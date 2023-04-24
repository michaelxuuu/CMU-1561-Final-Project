#include "uthread.h"

#include <stdio.h>
#include <stdint.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <stdatomic.h>
#include <sys/ucontext.h>

#define UTHREAD_STACK_SIZE (sizeof(char) * 1024 * 1024 * 8)

// asm.s
extern long _cas64(void *ptr, long oldval, long newval);
extern long _dcas32(void *ptr, int oldval[2], int newval[2]);

extern void _retain_call_stack();

enum {
    USTATE_RUNNING,
    USTATE_SLEEPING,
    USTATE_JOINABLE,
    USTATE_JOINED
};

struct uthread {
    int id;
    int wokerid;
};

// uthread context
struct uthread_context {
    int id;
    char *name;
    short state;
    short marker;
    struct uthread_context *next;
    void *stack;
    void *scratch;
    void *ret;
    ucontext_t *uc;
};

struct garbage {
    struct uthread_context *ucon;
    struct garbage *next;
    int epoch;
};

// a circular singly-linked list of uthread contexts
struct ulist {
    atomic_int size;
    atomic_int dead_count;
    struct uthread_context *head;
    int refct;
    int epoch;
    struct garbage *ghead;
};

// worker data
static __thread int id_threadlocal; // integer worker id assigned and used by the run-time
struct worker {
    int id;
    pthread_t pthread_id;           // pthread id
    struct ulist ulis;              // work queue
    struct uthread_context *cur;    // pointer to the current context in execution
};

// thread local storage access wrappers
static inline int get_myid() { return id_threadlocal; }
static inline void set_myid(int myid) { id_threadlocal = myid; }

// run-time data
static struct {
    pthread_t master;
    int is_active;
    int worker_count;      // number of cores
    struct worker *workers;     // a pool of worker
    atomic_uint next_uid;       // id to hand to the next uthread created
    atomic_uint next_worker;    // worker to give the next uthread to
    atomic_uint ready_count;
} runtime = {
    .is_active = 0,
    .worker_count = 0,
    .workers = 0,
    .next_uid = 0,
    .next_worker = 0,
    .ready_count = 0
};

// helper functions
// generate a new uthread id
static inline int gen_uid() { return runtime.next_uid++; }
// pick a worker from the pool to take on the next piece of work (uthread)
static inline struct worker *pick_worker() { return &runtime.workers[(runtime.next_worker++ % runtime.worker_count)]; }

// in this handler, a worker thread will relay SIGARLM to the main thread
// the main thread will broadcast SIGUSR1 to each worker threads to start their scheduler
void sigalrm_handler(int signum) {
    if (pthread_self() != runtime.master) {
        pthread_kill(runtime.master, SIGALRM);
    }
    else {
        for (int i = 0; i < runtime.worker_count; i++)
            pthread_kill(runtime.workers[i].pthread_id, SIGUSR1);
    }
}

// uthread schedualer (installed as the SIGUSR1 handler)
void scheduler(int signum, siginfo_t *si, void *context) {
    ucontext_t *uc = (ucontext_t *)context;
    uc = (ucontext_t *)context;
    struct worker *w = &runtime.workers[get_myid()];

    // save the context of the running uthread
    if (w->cur->state == USTATE_RUNNING) {
        memcpy(w->cur->uc, uc, sizeof(ucontext_t));
        w->cur->state = USTATE_SLEEPING;
    }

    // find a sleeping uthread
    struct uthread_context *p = w->cur->next;
    for (; p != w->cur; p = p->next)
        if (p->state == USTATE_SLEEPING)
            break;
    w->cur = p;

    w->cur->state = USTATE_RUNNING;

    // load the context of the next uthread
    // queue's never empty guaranteed by introcuding the dummy node
    memcpy(uc, w->cur->uc, sizeof(ucontext_t));
}

// initailze a worker (used on creation)
static void* worker_init(void *id) {
    // install thread-lcoal id
    set_myid((uintptr_t)id); // id is poassed by value

    // insatll signal handlers
    struct sigaction sa_alrm;
    struct sigaction sa_usr1;

    memset(&sa_alrm, 0, sizeof(sa_alrm));
    memset(&sa_usr1, 0, sizeof(sa_usr1));
    
    sa_usr1.sa_flags = SA_SIGINFO;
    sa_usr1.sa_sigaction = scheduler;
    sigfillset(&sa_usr1.sa_mask);
    sigaction(SIGUSR1, &sa_usr1, NULL);

    sa_alrm.sa_handler = sigalrm_handler;
    sigfillset(&sa_alrm.sa_mask);
    sigaction(SIGALRM, &sa_alrm, NULL);

    runtime.ready_count++;

    struct worker *w = &runtime.workers[get_myid()];
    struct ulist *ulis = &w->ulis;
    while (1) {
        struct uthread_context *cur = ulis->head->next;
        while(cur->next != ulis->head) {
            if (cur->next->state == USTATE_JOINED){
                struct uthread_context *tmp = cur->next;
                cur->next = cur->next->next;
                struct garbage *grbg = reentrant_malloc(sizeof(struct garbage));
                grbg->epoch = ulis->epoch;
                grbg->ucon = tmp;
                grbg->next = ulis->ghead->next;
                ulis->ghead->next = grbg;
            } else if (cur->next->state == USTATE_JOINABLE && cur->next->uc) {
                free(cur->next->uc);
                cur->next->uc = 0;
            }
            cur = cur->next;
        }
    }
}

static void* garbage_collector(void *id) {
    struct worker *w = &runtime.workers[(uintptr_t)id];
    struct ulist *ulis = &w->ulis;
    while (1) {
        struct garbage *cur = ulis->ghead->next;
        while(cur->next != ulis->ghead) {
            if (cur->next->epoch < ulis->epoch){
                struct garbage *tmp = cur->next;
                cur->next = tmp->next;
                free(tmp->ucon);
                free(tmp);
            }
            cur = cur->next;
        }
    }
}

// initialize the run-time (invoked by the first call to uthread_create, used internally)
void runtime_init() {
    runtime.master = pthread_self();
    runtime.is_active = 1;
    runtime.next_uid = 0;
    // start as many cores as workers
    runtime.worker_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
    runtime.workers = malloc(sizeof(struct worker) * runtime.worker_count);

    for (size_t i = 0; i < runtime.worker_count; i++) {
        struct worker *w = &runtime.workers[i];
        struct ulist *ulis = &w->ulis;
        // worker initializer that also deletes joined uthreads from the work queue
        struct uthread_context *ucon = malloc(sizeof(struct uthread_context));
        memset(ucon, 0, sizeof(struct uthread_context));
        ucon->uc = malloc(sizeof(struct ucontext_t));
        memset(ucon->uc, 0, sizeof(ucontext_t));
        ucon->id = gen_uid();
        ucon->state = USTATE_RUNNING;
        ucon->name = "dummy1";
        ulis->head = ucon;
        ulis->size = 1;
        ulis->dead_count = 0;
        ulis->refct = 0;
        ulis->epoch = 0;
        ulis->ghead = malloc(sizeof(struct garbage)); // dummy garbage node
        memset(ulis->ghead, 0, sizeof(struct garbage));
        ulis->ghead->next = ulis->ghead;
        ucon->next = ucon;
        w->cur = ucon;
        w->id = i;
        // garbage collector that frees the memory that's no longer referenced
        ucon = malloc(sizeof(struct uthread_context));
        memset(ucon, 0, sizeof(struct uthread_context));
        ucon->uc = malloc(sizeof(ucontext_t));
        memset(ucon->uc, 0, sizeof(ucontext_t));
        ucon->id = gen_uid();
        ucon->state = USTATE_SLEEPING;
        ucon->name = "dummy2";
        ucon->stack = malloc(UTHREAD_STACK_SIZE);
        void *stack = ucon->stack;
        stack = (void *)((uintptr_t)stack + UTHREAD_STACK_SIZE);
        uint64_t cs, ss;
        __asm__ volatile (
            "mov %%cs, %0;"
            "mov %%ss, %1;"
            :"=r"(cs), "=r"(ss)
        );
        ucon->uc->uc_mcontext.gregs[REG_RSP] = (greg_t)stack;
        ucon->uc->uc_mcontext.gregs[REG_RIP] = (greg_t)garbage_collector;
        ucon->uc->uc_mcontext.gregs[REG_RDI] = i;
        ucon->uc->uc_mcontext.gregs[REG_CSGSFS] = cs | (ss << 48);
        ulis->head->next = ucon;
        ucon->next = ulis->head;
        // initailize the worker
        pthread_create(&w->pthread_id, NULL, worker_init, (void *)i); // cast to (void *) to pass by value
    }

    // set up timer
    struct sigaction sa;
    struct itimerval timer;

    // Configure the timer signal handler
    sa.sa_handler = sigalrm_handler;
    memset(&sa, 0, sizeof(sa));
    sigfillset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);

#define MS (1000)
    // Configure the timer to fire every 10ms 10000
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = MS * 10;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = MS * 10;

    while (runtime.ready_count != runtime.worker_count);
    setitimer(ITIMER_REAL, &timer, NULL);
}

// clean up the resource allcoated for the uthread
void cleanup() {
    void *tmp;
    asm ("mov %%rax, %0" : "=r"(tmp));
    struct uthread_context *ucon = runtime.workers[get_myid()].cur;
    ucon->ret = tmp;
    // swap stack
    asm (
        ".extern reentrant_free;"
        "mov %0, %%rdi;"
        "mov %1, %%rsp;"
        "mov %%rsp, %%rbp;"
        "call reentrant_free;"
        :: "r"(ucon->stack), "r"(((long)ucon->scratch + 4096))
    );

    runtime.workers[get_myid()].cur->state = USTATE_JOINABLE;
    for(;;);
}

// allocate resources for a new thread and returns its id
void uthread_create(uthread_t *id, void *(*func)(void *), void *arg) {

    if (!runtime.is_active)
        runtime_init();

    struct uthread_context *ucon = malloc(sizeof(struct uthread_context));
    memset(ucon, 0, sizeof(struct uthread_context));
    ucon->uc = malloc(sizeof(ucontext_t));
    memset(ucon->uc, 0, sizeof(ucontext_t));
    
    ucon->stack = malloc(UTHREAD_STACK_SIZE);
    ucon->scratch = malloc(4096);
    ucon->name = "uthread";
    memset(ucon->stack, 0, UTHREAD_STACK_SIZE);

    ucon->stack = malloc(UTHREAD_STACK_SIZE);
    memset(ucon->stack, 0, UTHREAD_STACK_SIZE);

    void *rsp = (void *)(((uintptr_t)ucon->stack + UTHREAD_STACK_SIZE & ~0xFL) - 8);
    *(uintptr_t *)rsp = (uintptr_t)cleanup;

    uint64_t cs, ss;
    __asm__ volatile (
        "mov %%cs, %0;"
        "mov %%ss, %1;"
        :"=r"(cs), "=r"(ss)
    );

    ucon->uc->uc_mcontext.gregs[REG_RSP] = (greg_t)rsp;
    ucon->uc->uc_mcontext.gregs[REG_RDI] = (greg_t)arg;
    ucon->uc->uc_mcontext.gregs[REG_RIP] = (greg_t)func;
    ucon->uc->uc_mcontext.gregs[REG_CSGSFS] = cs | (ss << 48);
    
    ucon->id = gen_uid();
    ucon->state = USTATE_SLEEPING;
    ucon->marker = 0;

    struct worker *w = pick_worker();
    struct ulist *ulis = &w->ulis;
    struct uthread_context *head = ulis->head;

    struct uthread_context *old_next;
    do {
        old_next = head->next;
        ucon->next = old_next;
    } while (_cas64(&head->next, (long)old_next, (long)ucon) != (long)old_next);
    ulis->size++;

    *id = *(uthread_t *)&(struct uthread){ucon->id, w->id};
}

int uthread_join(uthread_t id, void *ret) {
    struct uthread u = *(struct uthread *)&id;
    struct ulist *ulis = &runtime.workers[u.wokerid].ulis;

    long oldval, newval, actual;
    do {
        oldval = ulis->refct | (long)ulis->epoch << 32; // load the old value
        newval = oldval + 1; // grab a reference (incr refct)
        if (ulis->refct == 0) newval = newval + (1L << 32); // advance to the next epoch if at the end of the current epoch (refct = 0)
    } while (_cas64(&ulis->refct, (long)oldval, (long)newval) != (long)oldval);

    struct uthread_context *head = ulis->head;
    for (struct uthread_context *p = head->next; p != head; p = p->next) {
        if (p->id == u.id) {
            do {
                oldval = ulis->refct | (long)ulis->epoch << 32; // load the old value
                if (ulis->refct == 0) newval = newval + (1L << 32); // advance to the next epoch if at the end of the current epoch (refct = 0)
            } while (_cas64(&ulis->refct, (long)oldval, (long)newval) != (long)oldval);
            ulis->refct--; // release the reference
            while (p->state != USTATE_JOINABLE);
            if (ret) *(uintptr_t *)ret = (uintptr_t)p->ret;
            // cleanup must has finished by not since the state has been changed to USTATE_JOINABLE
            // so that the scratch space is no longer needed
            free(p->scratch); // must free first
            p->state = USTATE_JOINED;
            ulis->dead_count++;
            return 0;
        }
    }
    do {
        oldval = ulis->refct | (long)ulis->epoch << 32; // load the old value
        if (ulis->refct == 0) newval = newval + (1L << 32); // advance to the next epoch if at the end of the current epoch (refct = 0)
    } while (_cas64(&ulis->refct, (long)oldval, (long)newval) != (long)oldval);
    ulis->refct--; // release the reference
    return -1;
}

int uthread_detach(uthread_t id) {
    
}

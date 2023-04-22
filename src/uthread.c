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
extern long _cas32(void *ptr, int oldval, int newval);
extern long _cas64(void *ptr, long oldval, long newval);
extern int _dcas64(void *ptr, long oldval[2], long newval[2], long actual[2]);

#define align16(x) ((x + 15UL) & ~15UL)

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
    short state;
    short marker;
    struct uthread_context *next;
    struct uthread_context *prev;
    void *stack;
    void *ret;
    ucontext_t uc;
};

// a circular queue of uthread contexts
struct uqueue {
    atomic_int size;
    atomic_int dead_count;
    struct uthread_context *head;
};

// worker data
static __thread int id_threadlocal; // integer worker id assigned and used by the run-time
struct worker {
    int id;
    pthread_t pthread_id;           // pthread id
    struct uqueue queue;            // work queue
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
        memcpy(&w->cur->uc, uc, sizeof(ucontext_t));
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
    memcpy(uc, &w->cur->uc, sizeof(ucontext_t));
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
    struct uqueue *q = &w->queue;
    while (1) {
        struct uthread_context *cur = q->head->next->next;
        while(cur != q->head) {
            if (cur->state == USTATE_JOINED){
                struct uthread_context *tofree = cur;
                cur = cur->prev;
                tofree->prev->next = tofree->next;
                tofree->next->prev = tofree->prev;
                free(tofree);
            } else if (cur->state == USTATE_JOINABLE && cur->stack){
                free(cur->stack);
                cur->stack = 0;
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
        struct uqueue *q = &w->queue;
        // create dummy context for the worker to exeucte when idle
        // so that it does not have to be blocked in signal handler
        struct uthread_context *ucon = malloc(align16(sizeof(struct uthread_context)));
        memset(ucon, 0, sizeof(struct uthread_context));

        ucon->state = USTATE_RUNNING;
        q->head = ucon;
        q->size = 1;
        q->dead_count = 0;
        ucon->next = ucon;
        ucon->prev = ucon;
        w->cur = ucon;
        w->id = i;
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
static void cleanup() {
    void *tmp;
    asm ("mov %%rax, %0" : "=r"(tmp));
    struct worker *w = &runtime.workers[get_myid()];
    struct uqueue *q = &w->queue;
    struct uthread_context *ucon = w->cur;
    ucon->ret = tmp;
    ucon->state = USTATE_JOINABLE;
    // yeild
    pthread_kill(runtime.workers[get_myid()].pthread_id, SIGUSR1);
    for(;;);
}

// allocate resources for a new thread and returns its id
void uthread_create(uthread_t *id, void *(*func)(void *), void *arg) {

    if (!runtime.is_active)
        runtime_init();

    struct uthread_context *ucon = malloc(align16(sizeof(struct uthread_context)));
    memset(ucon, 0, sizeof(struct uthread_context));
    
    ucon->stack = malloc(UTHREAD_STACK_SIZE);
    memset(ucon->stack, 0, UTHREAD_STACK_SIZE);

    void *stack = ucon->stack;
    stack = (void *)((uintptr_t)stack + UTHREAD_STACK_SIZE);
    *(uintptr_t *)(--stack) = (uintptr_t)cleanup;


    uint64_t cs, ss;
    __asm__ volatile (
        "mov %%cs, %0;"
        "mov %%ss, %1;"
        :"=r"(cs), "=r"(ss)
    );

    ucon->uc.uc_mcontext.gregs[REG_RSP] = (greg_t)stack;
    ucon->uc.uc_mcontext.gregs[REG_RDI] = (greg_t)arg;
    ucon->uc.uc_mcontext.gregs[REG_RIP] = (greg_t)func;
    ucon->uc.uc_mcontext.gregs[REG_CSGSFS] = cs | (ss << 48);
    
    ucon->id = runtime.next_uid++;
    ucon->state = USTATE_SLEEPING;
    ucon->marker = 0;

    struct worker *w = pick_worker();
    struct uqueue *q = &w->queue;
    struct uthread_context *head = q->head;

    struct uthread_context *old_head;
    do {
        old_head = head->next;
        ucon->prev = head;
        ucon->next = old_head;
    } while (_cas64(&head->next, (long)old_head, (long)ucon) != (uint64_t)ucon->next);
    old_head->prev = ucon;
    q->size++;

    *id = *(uthread_t *)&(struct uthread){ucon->id, w->id};
}

int uthread_join(uthread_t id, void *ret) {
    struct uthread u = *(struct uthread *)&id;
    struct uqueue *q = &runtime.workers[u.wokerid].queue;
    struct uthread_context *head = q->head;
    for (struct uthread_context *p = head->next; p != head; p = p->next) {
        if (p->id == u.id) {
            while (p->state != USTATE_JOINABLE);
            if (ret) *(uintptr_t *)ret = (uintptr_t)p->ret;
            p->state = USTATE_JOINED;
            q->dead_count++;
            return 0;
        }
    }
    return -1;
}

int uthread_detach(uthread_t id) {
    
}

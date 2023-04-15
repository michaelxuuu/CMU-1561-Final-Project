#include "x86_64.h"
#include "uthread.h"

#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <sys/_types/_ucontext.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <stdatomic.h>

#define MAX_UTHREAD_COUNT 128
#define UTHREAD_STACK_SIZE (sizeof(char) * 1024 * 1024 * 8)



enum {
    USTATE_RUNNING,
    USTATE_SLEEPING,
    USTATE_ZOMBIE,
    USTATE_DEAD
};

struct uthread {
    uint32_t id;
    uint32_t wokerid;
};

// uthread context
struct uthread_context {
    uint32_t id;
    uint32_t state;
    ucontext_t uc;
    void *stack;
    void *ret;
    struct uthread_context *prev;
    struct uthread_context *next;
};

// a circular queue of uthread contexts
struct uqueue {
    atomic_int size;
    atomic_int dead_count;
    struct uthread_context *head;
};

// worker data
static __thread uint32_t id_threadlocal; // integer worker id assigned and used by the run-time
static __thread uint32_t next_id_threadlocal; // next worker to forward the signal to
struct worker {
    uint32_t id;
    pthread_t pthread_id;           // pthread id
    struct uqueue queue;            // work queue
    struct uthread_context *cur;    // pointer to the current context in execution
};

// run-time data
static struct {
    pthread_t master;
    uint32_t is_active;
    uint32_t worker_count;      // number of cores
    struct worker *workers;     // a pool of worker
    atomic_uint next_uid;       // id to hand to the next uthread created
    atomic_uint next_worker;    // worker to give the next uthread to
    atomic_uint sched_count;    // call count to scheduale() in the current scheduling round
    atomic_uint init_count;
} runtime = {
    .is_active = 0,
    .worker_count = 0,
    .workers = 0,
    .next_uid = 0,
    .next_worker = 0,
    .sched_count = 0,
    .init_count = 0
};

sigset_t enter_critical() {
    sigset_t oldmask, newmask;
    sigfillset(&newmask);
    pthread_sigmask(SIG_BLOCK, &newmask, &oldmask);
    return oldmask;
}

void exit_critical(sigset_t oldmask) {
    pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
}


// helper functions
// generate a new uthread id
static inline uint32_t gen_uid() { return runtime.next_uid++; }
// pick a worker from the pool to take on the next piece of work (uthread)
static inline struct worker *pick_worker() { return &runtime.workers[(runtime.next_worker++ % runtime.worker_count)]; }

// thread local storage access wrappers
static inline uint32_t get_myid() { return id_threadlocal; }
static inline uint32_t get_nextid() { return next_id_threadlocal; }
static inline void set_myid(uint32_t myid) { id_threadlocal = myid; }
static inline void set_nextid(uint32_t myid) { next_id_threadlocal = (myid == runtime.worker_count-1) ? 0 : myid + 1; }

extern uint64_t CAS(uint64_t addr, uint64_t old_val, uint64_t new_val);
extern uint64_t DCAS(uint64_t addr, uint32_t old_val[2], uint32_t new_val[2]);

__asm__ (
    "_CAS:;"
    "mov %rsi, %rax;"
    "mov %rdx, %rsi;"
    "lock cmpxchgq %rsi, (%rdi);"
    "ret;"
);

__asm__ (
    "_DCAS:;"
    
    "mov 4(%rsi), %eax;" // higher half - old_val[2]
    "shl $32, %rax;"
    "mov (%rsi), %ebx;" // lower half - old_val[1]
    "or %rbx, %rax;"

    "mov 4(%rdx), %esi;" // higher half - new_val[2]
    "shl $32, %rsi;"
    "mov (%rdx), %ebx;" // lower half - old_val[1]
    "or %rbx, %rsi;"

    "lock cmpxchgq %rsi, (%rdi);"
    "ret;"
);


// exeucted by the main pthread
// main pthread fowards SIGUSR1 to each of the worker pthreads
void sigalarm_handler(int signum) {
    for (int i = 0; i < runtime.worker_count; i++)
        pthread_kill(runtime.workers[i].pthread_id, SIGUSR1);
}

// uthread schedualer (installed as the SIGUSR1 handler)
void scheduler(int signum, siginfo_t *si, void *context) {

    ucontext_t *uc = (ucontext_t *)context;
    uc = (ucontext_t *)context;
    struct worker *w = &runtime.workers[get_myid()];

    // save the context of the running uthread
    if (w->cur->state == USTATE_RUNNING) {
        memcpy(&w->cur->uc, uc, sizeof(ucontext_t));
        memcpy(w->cur->uc.uc_mcontext, uc->uc_mcontext, sizeof(_STRUCT_MCONTEXT64));
        w->cur->state = USTATE_SLEEPING;
    }

    // find a sleeping uthread
    struct uthread_context *p = w->cur->next;
    for (; p != w->cur; p = p->next)
        if (p->state == USTATE_SLEEPING && p != w->queue.head)
            break;
    if (p == w->cur && w->cur->state != USTATE_SLEEPING) w->cur = w->queue.head;
    else w->cur = p;

    w->cur->state = USTATE_RUNNING;

    // load the context of the next uthread
    // queue's never empty guaranteed by introcuding the dummy node
    memcpy(uc, &w->cur->uc, sizeof(ucontext_t));
    memcpy(uc->uc_mcontext, w->cur->uc.uc_mcontext, sizeof(_STRUCT_MCONTEXT64));
}

// initailze a worker (used on creation)
static void* worker_init(void *arg) {
    uintptr_t id = (uintptr_t)arg;

    // install thread-lcoal id
    set_myid((uint32_t)id); // id is poassed by value
    set_nextid((uint32_t)id);

    // insatll signal handlers
    struct sigaction sa_alrm;
    struct sigaction sa_usr1;

    memset(&sa_alrm, 0, sizeof(sa_alrm));
    memset(&sa_usr1, 0, sizeof(sa_usr1));
    
    sa_alrm.sa_flags = SA_SIGINFO;
    sa_alrm.sa_sigaction = scheduler;
    sigfillset(&sa_alrm.sa_mask);
    sigaction(SIGUSR1, &sa_alrm, NULL);

    sa_usr1.sa_handler = sigalarm_handler;
    sigfillset(&sa_usr1.sa_mask);
    sigaction(SIGALRM, &sa_usr1, NULL);

    // trigger scheduler maunally, if timer hasn't expired yet
    // pthread_kill(runtime.workers[get_myid()].pthread_id, SIGUSR1);
    runtime.init_count++;

    struct worker *w = &runtime.workers[get_myid()];
    struct uqueue *q = &w->queue;
    struct uthread_context *cur = q->head->next;
    while(cur != q->head) {
        if (cur->state == USTATE_DEAD){
            cur->prev->next = cur->next;
            cur->next->prev = cur->prev;
            free(cur);
        }
        if (cur->state == USTATE_ZOMBIE){
            free(cur->uc.uc_mcontext);
            free(cur->stack);
        }
    }
}

// initialize the run-time (invoked by the first call to uthread_create, used internally)
static void runtime_init() {
    runtime.master = pthread_self();
    runtime.is_active = 1;
    runtime.next_uid = 0;
    // start as many cores as workers
    runtime.worker_count = (uint32_t)sysconf(_SC_NPROCESSORS_ONLN);
    runtime.workers = malloc(sizeof(struct worker) * runtime.worker_count);
    runtime.next_worker = 0;

    for (size_t i = 0; i < runtime.worker_count; i++) {
        struct worker *w = &runtime.workers[i];
        struct uqueue *q = &w->queue;
        // create dummy context for the worker to exeucte when idle
        // so that it does not have to be blocked in signal handler
        struct uthread_context *ucon = malloc(sizeof(struct uthread_context));
        memset(ucon, 0, sizeof(struct uthread_context));

        ucon->uc.uc_mcontext = malloc(sizeof(_STRUCT_MCONTEXT64));
        memset(ucon->uc.uc_mcontext, 0, sizeof(_STRUCT_MCONTEXT64));

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
    sa.sa_handler = sigalarm_handler;
    memset(&sa, 0, sizeof(sa));
    sigfillset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);

    // Configure the timer to fire every 10ms 10000
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 1;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 1;

    while (runtime.init_count != runtime.worker_count);
    setitimer(ITIMER_REAL, &timer, NULL);
    asm ("de:");
}

// clean up the resource allcoated for the uthread
static void cleanup() {
    void *tmp;
    asm ("mov %%rax, %0" : "=r"(tmp));
    struct worker *w = &runtime.workers[get_myid()];
    struct uqueue *q = &w->queue;
    struct uthread_context *ucon = w->cur;
    ucon->ret = tmp;
    ucon->state = USTATE_ZOMBIE;
    // yeild
    pthread_kill(runtime.workers[get_myid()].pthread_id, SIGUSR1);
    for(;;);
}

// allocate resources for a new thread and returns its id
int uthread_create(uthread_t *id, void *(*func)(void *), void *arg) {

    if (!runtime.is_active)
        runtime_init();

    struct uthread_context *ucon = malloc(sizeof(struct uthread_context));
    memset(ucon, 0, sizeof(struct uthread_context));

    ucon->uc.uc_mcontext = malloc(sizeof(_STRUCT_MCONTEXT64));
    memset(ucon->uc.uc_mcontext, 0, sizeof(_STRUCT_MCONTEXT64));
    
    ucon->stack = malloc(UTHREAD_STACK_SIZE);
    memset(ucon->stack, 0, UTHREAD_STACK_SIZE);

    uint64_t *stack = ucon->stack;
    stack = (uint64_t *)((uint64_t)stack + UTHREAD_STACK_SIZE);
    *(--stack) = (uint64_t)cleanup;
    
    mcontext_t mc = ucon->uc.uc_mcontext;
    mc_set_rsp(mc, (uint64_t)(stack));
    mc_set_rdi(mc, (uint64_t)arg);
    mc_set_rip(mc, (uint64_t)func);
    ucon->id = runtime.next_uid++;
    ucon->state = USTATE_SLEEPING;

    struct worker *w = pick_worker();
    struct uqueue *q = &w->queue;
    struct uthread_context *head = q->head;


    struct uthread_context *old_head;
    do {
        old_head = head->next;
        ucon->prev = head;
        ucon->next = old_head;
    } while (CAS((uint64_t)&head->next, (uint64_t)old_head, (uint64_t)ucon) != (uint64_t)ucon->next);
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
            while (p->state != USTATE_ZOMBIE);
            if (ret) *(uintptr_t *)ret = (uintptr_t)p->ret;
            p->state == USTATE_DEAD;
            q->dead_count++;
            return 0;
        }
    }
    return -1;
}

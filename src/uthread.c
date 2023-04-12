#include "ucon.h"
#include "uthread.h"

#include <stdio.h>
#include <stdint.h>
#include <signal.h>
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
    USTATE_ZOMBIE
};

// a circular queue of uthread contexts
struct uqueue {
    uint32_t size;
    struct uthread_context *head;
};

// worker data
static __thread uint32_t id_threadlocal; // integer worker id assigned and used by the run-time
static __thread uint32_t next_id_threadlocal; // next worker to forward the signal to
struct worker {
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
} runtime = {
    .is_active = 0,
    .worker_count = 0,
    .workers = 0,
    .next_uid = 0,
    .next_worker = 0,
    .sched_count = 0
};

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
    if (w->cur) {
        memcpy(&w->cur->uc, uc, sizeof(ucontext_t));
        memcpy(w->cur->uc.uc_mcontext, uc->uc_mcontext, sizeof(_STRUCT_MCONTEXT64));
        w->cur = w->cur->next;
    } else w->cur = w->queue.head;

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
    sigaction(SIGUSR1, &sa_alrm, NULL);

    sa_usr1.sa_handler = sigalarm_handler;
    sigemptyset(&sa_usr1.sa_mask);
    sigaction(SIGALRM, &sa_usr1, NULL);

    // trigger scheduler maunally, if timer hasn't expired yet
    // pthread_kill(runtime.workers[get_myid()].pthread_id, SIGUSR1);

    // *** to be changed to using sigsuspend
    sigset_t mask;
    sigemptyset(&mask);
    for(;;) {
        sigsuspend(&mask);
    } // do not exit (worker_init has no caller)
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
        struct worker *p = &runtime.workers[i];
        p->queue.head = 0;
        p->queue.size = 0;
        // create dummy context for the worker to exeucte when idle
        // so that it does not have to be blocked in signal handler
        struct uthread_context *ucon = malloc(sizeof(struct uthread_context));
        memset(ucon, 0, sizeof(struct uthread_context));
        ucon->uc.uc_mcontext = malloc(sizeof(_STRUCT_MCONTEXT64));
        memset(ucon->uc.uc_mcontext, 0, sizeof(_STRUCT_MCONTEXT64));
        ucon->id = runtime.next_uid++;
        struct worker *w = &runtime.workers[i];
        struct uqueue *q = &w->queue;
        q->head = ucon;
        q->size = 1;
        ucon->next = ucon;
        ucon->prev = ucon;
        w->cur = ucon;
        // initailize the worker
        pthread_create(&p->pthread_id, NULL, worker_init, (void *)i); // cast to (void *) to pass by value
    }

    // set up timer
    struct sigaction sa;
    struct itimerval timer;

    // Configure the timer signal handler
    sa.sa_handler = sigalarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);

    // Configure the timer to fire every 500ms
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 500000;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 500000;

    // Start the timer
    setitimer(ITIMER_REAL, &timer, NULL);
}

sigset_t begin_critical(int signo) {
    sigset_t oldmask, newmask;
    sigemptyset(&newmask);
    sigaddset(&newmask, signo);
    pthread_sigmask(SIG_BLOCK, &newmask, &oldmask);
    return oldmask;
}

void end_critical(sigset_t oldmask) {
    pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
}

// clean up the resource allcoated for the uthread
static void cleanup() {
    struct worker *w = &runtime.workers[get_myid()];
    struct uqueue *q = &w->queue;
    struct uthread_context *ucon = w->cur;

    // mask signal
    sigset_t oldmask = begin_critical(SIGUSR1);

    // dummy guarantees that the queue's never empty 
    ucon->prev->next = ucon->next;
    ucon->next->prev = ucon->prev;
    q->head = ucon->next;

    ucon->next = ucon->prev = 0;
    free(w->cur->uc.uc_mcontext);
    free(w->cur);

    w->cur = 0; // set it to NULL, schedualer won't save this context

    end_critical(oldmask);
    // unmask signal

    // trigger scheduler maunally, if timer hasn't expired yet
    // pthread_kill(runtime.workers[get_myid()].pthread_id, SIGUSR1);

    // *** to be changed to using sigsuspend
    sigset_t mask;
    sigemptyset(&mask);
    for(;;) {
        sigsuspend(&mask);
    } // do not exit (cleanup has no caller)
}

// allocate resources for a new thread and returns its id
uint32_t uthread_create(void *(*func)(void *), void *arg) {

    if (!runtime.is_active)
        runtime_init();

    struct uthread_context *ucon = malloc(sizeof(struct uthread_context));
    memset(ucon, 0, sizeof(struct uthread_context));
    ucon->uc.uc_mcontext = malloc(sizeof(_STRUCT_MCONTEXT64));
    memset(ucon->uc.uc_mcontext, 0, sizeof(_STRUCT_MCONTEXT64));
    
    mcontext_t mc = ucon->uc.uc_mcontext;
    uint64_t *stack = (uint64_t *)((uint64_t)malloc(UTHREAD_STACK_SIZE) + UTHREAD_STACK_SIZE);
    *(--stack) = (uint64_t)cleanup;
    mc_set_rsp(mc, (uint64_t)(stack));
    mc_set_rdi(mc, (uint64_t)arg);
    mc_set_rip(mc, (uint64_t)func);
    ucon->id = runtime.next_uid++;
    ucon->state = USTATE_SLEEPING;

    struct uqueue *q = &pick_worker()->queue;

    // mask signal
    sigset_t oldmask = begin_critical(SIGUSR1);
    // dummy guarantees that the queue's at least having one element at any given time
    ucon->next = q->head;
    ucon->prev = q->head->prev;
    q->head->prev->next = ucon;
    q->head->prev = ucon;
    q->head = ucon;
    // unmask signal
    end_critical(oldmask);

    /**** test ****/
    // pthread_kill(runtime.workers[0].pthread_id, SIGUSR1);
    return ucon->id;
}

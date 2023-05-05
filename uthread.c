#include "uthread.h"

#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ucontext.h>

#define ALIGN16(x) ((uint64_t)(x) & ~0xFL) // round down to a multiple of 16
#define UTHREAD_STACK_SIZE (1024 * 1024 * 8)
#define ALTSTACK_SIZE (4096)

// asm.s
extern uint64_t _cas(void *ptr, uint64_t oldval, uint64_t newval);
extern uint16_t _ss();
extern uint16_t _cs();

// umalloc.c
extern void *_umalloc(uint64_t size);

extern void _cleanup();

enum {
    USTATE_RUNNING,
    USTATE_SLEEPING,
    USTATE_JOINABLE,
    USTATE_JOINED
};

struct uthread {
    int id;
    volatile int state;
    int detached;
    char *stack;    // address returned by memory allocation function (not stack base address)
    void *retptr;
    ucontext_t ucon;
    struct uthread *next;
};

static __thread int worker_id; // local to each pthread (worker)
struct worker {
    int id;
    pthread_t pthread_id; // the pthread that the worker maps to
    struct uthread *head; // work queue
    struct uthread *cur;  // current uthread in execution
    void *altstack;       // per-worker signal handler stack
};

static struct {
    pthread_t master;
    int worker_count;
    atomic_uint started;
    atomic_uint next_uid;
    atomic_uint next_worker;
    atomic_uint ready_count;
    struct worker *workers;
} runtime = {0, 0, 0, 0, 0, 0, 0};

void sigalrm_handler(int signum) {
    if (pthread_self() != runtime.master) { // not master
        pthread_kill(runtime.master, SIGALRM); // forward SIGALRM to the master thread
    } else {
        // bcast SIGUSR1 to all workers to start the scheduler
        for (int i = 0; i < runtime.worker_count; i++) {
            pthread_kill(runtime.workers[i].pthread_id, SIGUSR1);
        }
    }
}

void scheduler(int signum, siginfo_t *si, void *context) {
    ucontext_t *ucon = (ucontext_t *)context;
    struct worker *w = &runtime.workers[worker_id];
    // if the currnet thread is still in RUNNING state (have yet to finish exeucting cleanup())
    // save its context
    if (w->cur->state == USTATE_RUNNING) {
        // swap in the most recent context from cpu to replace the stale one in the work queue
        memcpy(&w->cur->ucon, ucon, sizeof(ucontext_t));
        // put it sleep
        w->cur->state = USTATE_SLEEPING;
    }

    // starting from the next, find a sleeping uthread
    struct uthread *p = w->cur->next;
    for (; p != w->cur; p = p->next)
        if (p->state == USTATE_SLEEPING) 
            break;
    w->cur = p;
    
    // wake up!
    w->cur->state = USTATE_RUNNING;

    // load the execution context
    // queue's never empty, guaranteed by introcuding the dummy node
    memcpy(ucon, &w->cur->ucon, sizeof(ucontext_t));
}

// a "dummy" uthread that's not "dummy"
void *dummy(void *id) {
    // initialize the thread-local worker id
    worker_id = (uint64_t)id;

    struct worker *w = &runtime.workers[worker_id];

    // use alt stack for signal handling
    stack_t altstack;
    altstack.ss_sp = w->altstack;
    altstack.ss_size = ALTSTACK_SIZE;
    altstack.ss_flags = SS_ONSTACK;
    sigaltstack(&altstack, 0);
    
    runtime.ready_count++;

    // delete joined uthreads from the work queue
    for (;;)
        for (struct uthread *p = w->head->next; p != w->head; p = p->next) {
            if (p->next->state == USTATE_JOINED || (p->next->state == USTATE_JOINABLE && p->next->detached)) {
                // this deletion is not atomic, so we have to first make the uthread unreachable for the scheduler
                // and then deallcoate the memory
                struct uthread *tofree = p->next;
                p->next = p->next->next; // make the uthread unreachable
                ufree(tofree);
            }
        }
}

void runtime_start() {
    runtime.master = pthread_self();
    runtime.started = 1;

    // get core count
    // runtime.worker_count = sysconf(_SC_NPROCESSORS_ONLN);
    runtime.worker_count = atoi(getenv("WORKERCT"));

    // start as many works as cores
    // allocate memory for workers
    runtime.workers = _umalloc(sizeof(struct worker) * runtime.worker_count);
    // initialize workers
    for (int id = 0 ; id < runtime.worker_count; id++) {
        struct worker *w = &runtime.workers[id];
        // install worker id
        w->id = id;
        // initialize the work queue
        // create a dummy uthread that will stay as the head of the work queue
        w->head = _umalloc(sizeof(struct uthread));
        // initialize the dummy uthread
        // this dummny uthread does not have an id and will never be exposed to the user
        w->head->next = w->head; // circular queue
        memset(&w->head->ucon, 0 , sizeof(ucontext_t));
        // set the dummy uthread as the current thread in execution
        w->head->state = USTATE_RUNNING;
        w->cur = w->head;
        // create alt stack for signal handler
        w->altstack = mmap(NULL, ALTSTACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
        // start the pthread
        pthread_create(&w->pthread_id, NULL, dummy, (void *)(uint64_t)id);
    }

    // install signal handlers
    struct sigaction sa_alrm;
    struct sigaction sa_usr1;

    memset(&sa_usr1, 0, sizeof(sa_usr1));
    sa_usr1.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
    sa_usr1.sa_sigaction = scheduler;
    sigfillset(&sa_usr1.sa_mask);
    sigaction(SIGUSR1, &sa_usr1, NULL);

    memset(&sa_alrm, 0, sizeof(sa_alrm));
    sa_alrm.sa_flags = SA_RESTART | SA_ONSTACK | SA_RESTART;
    sa_alrm.sa_handler = sigalrm_handler;
    sigfillset(&sa_alrm.sa_mask);
    sigaction(SIGALRM, &sa_alrm, NULL);

    // create a periodic timer
    struct itimerval timer;

    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 50;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 50;

    // start the timer
    while (runtime.ready_count != runtime.worker_count);
    setitimer(ITIMER_REAL, &timer, NULL);
}

__asm__ (
    ".globl _cleanup;"
    "_cleanup:;"
    "mov 0(%rsp), %r12;"  // &retptr
    "mov 8(%rsp), %r13;"  // &stack
    "mov 16(%rsp), %r14;" // &state
    "mov %rax, (%r12);"   // retptr = rax
    "mov (%r13), %rdi;"   // munmap param 1 (addr)
    "mov $(1024 * 1024 * 8), %rsi;" // munmap param 2 (len)
    "mov $0xb, %rax;"     // munmap syscall num is 11
    "syscall;"
    "movl $0x0, (%r13);"  // stack = 0
    "movl $0x2, (%r14);"  // state = 2 (JOINABLE)
    "jmp .;"
);

void uthread_create(uthread_t *id, void *(*func)(void *), void *arg) {

    if (!runtime.started)
        runtime_start();

    struct uthread *u = _umalloc(sizeof(struct uthread));
    memset(&u->ucon, 0, sizeof(ucontext_t));
    // assign id
    u->id = runtime.next_uid++;

    // allcoate stack on heap
    while (MAP_FAILED == (u->stack = mmap(NULL, UTHREAD_STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0)));
    // initailze the stack
    uint64_t *rsp = (uint64_t *)(ALIGN16(u->stack + UTHREAD_STACK_SIZE) - 8); // make rsp 8 byte aligned but not 16 byte aligned
    // prepare the return address
    *(--rsp) = (uint64_t)&u->state;
    *(--rsp) = (uint64_t)&u->stack;
    *(--rsp) = (uint64_t)&u->retptr;
    *(--rsp) = (uint64_t)_cleanup;
    // initialize the execution context
    u->ucon.uc_mcontext.gregs[REG_RSP] = (greg_t)rsp;
    u->ucon.uc_mcontext.gregs[REG_RDI] = (greg_t)arg;
    u->ucon.uc_mcontext.gregs[REG_RIP] = (greg_t)func;
    u->ucon.uc_mcontext.gregs[REG_CSGSFS] = _cs() | ((uint64_t)_ss() << 48);
    u->ucon.uc_stack.ss_size = ALTSTACK_SIZE;
    u->ucon.uc_stack.ss_flags = SS_ONSTACK;

    // initialize the state
    u->state = USTATE_SLEEPING;

    // not detached initially
    u->detached = 0;

    // pick a worker and insert the uthread into ist work queue
    struct worker *w = &runtime.workers[runtime.next_worker++ % runtime.worker_count];
    // install alt stack that will be used by the signal handler
    u->ucon.uc_stack.ss_sp = w->altstack;
    struct uthread *head = w->head;
    // add to the work queue (always add after the head)
    struct uthread *old_next;
    do {
        old_next = head->next; // the next uthread of the head is guaranteed to be valid
        u->next = old_next;
    } while (_cas(&head->next, (uint64_t)old_next, (uint64_t)u) != (uint64_t)old_next);

    *id = (uthread_t)u;
}

int uthread_join(uthread_t id, void **ret) {
    struct uthread *p = (struct uthread *)id;
    while (p->state != USTATE_JOINABLE);
    if (ret) *ret = p->retptr;
    p->state = USTATE_JOINED;
    return 0;
}

int uthread_detach(uthread_t id) {
    struct uthread *p = (struct uthread *)id;
    p->detached = 1;
    return 0;
}

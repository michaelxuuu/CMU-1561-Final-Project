#include "x86_64.h"
#include "uthread.h"

#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <stdatomic.h>

#define MAX_UTHREAD_COUNT 128
#define UTHREAD_ALIGNEMNT 16
#define UTHREAD_STACK_SIZE (sizeof(char) * 1024 * 1024 * 8)

enum {
    UTHREAD_STATE_RUNNING,
    UTHREAD_STATE_SLEEPING,
    UTHREAD_STATE_ZOMBIE,
};

// uthread context
struct uthread {
    uint32_t id;
    uint32_t state;
    ucontext_t uc;
};

// uthread queue node
struct node {
    struct node *prev;
    struct node *next;
    struct uthread uthd;
};

struct worker_id {
    pthread_t id1;
    uint32_t id2;
};

// worker data
struct worker {
    struct worker_id ids;
    pthread_mutex_t mu;
    struct node *queue;
    uint32_t qsize;
    uint32_t cur_pthread_id;
};

// worker ids (thread local)
static __thread struct worker_id worker_id;

// run-time data
static struct {
    uint32_t is_active;
    struct worker *worker_pool;
    uint32_t pool_size;
    atomic_uint next_uthread_id;
    atomic_uint next_worker_id;
} runtime;

// uthread schedualer (installed as the SIGALARM handler)
static void scheduler(int signum, siginfo_t *si, void *context) {
    ucontext_t *uc = (ucontext_t *) context;
    uc = (ucontext_t *) context;
    uc = (ucontext_t *) context;
    mcontext_t mc = uc->uc_mcontext;
    int a = worker_id.id2;
    if (runtime.worker_pool[0].cur_pthread_id == 0) {
        char *s1 = (char *)&runtime.worker_pool[0].queue[0].uthd.uc;
        char *s2 = (char *)runtime.worker_pool[0].queue[0].uthd.uc.uc_mcontext;
        for (int i = 0; i < sizeof(ucontext_t); i++)
            ((char *)uc)[i] = s1[i];
        for (int i = 0; i < sizeof(_STRUCT_MCONTEXT64); i++)
            ((char *)mc)[i] = s2[i];
        
    }
}

// initailze a worker (used on creation)
static void* worker_init(void *id) {
    worker_id.id1 = pthread_self();
    worker_id.id2 = *(uint32_t *)id;
    struct sigaction sa;
    sa.sa_sigaction = scheduler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGUSR1, &sa, NULL);
    // busy waiting
    for (;;);
}

// initialize the run-time (invoked by the first call to uthread_create, used internally)
static void runtime_init() {
    runtime.is_active = 1;
    runtime.next_uthread_id = 0;
    // start as many cores as workers
    runtime.pool_size = (uint32_t)sysconf(_SC_NPROCESSORS_ONLN);
    runtime.worker_pool = malloc(sizeof(struct worker) * runtime.pool_size);
    runtime.next_worker_id = 0;

    for (size_t i = 0; i < runtime.pool_size; i++) {
        runtime.worker_pool[i].ids.id2 = i;
        runtime.worker_pool[i].qsize = 0;
        runtime.worker_pool[i].queue = 0;
        pthread_mutex_init(&runtime.worker_pool[i].mu, 0);
        pthread_create(&runtime.worker_pool[i].ids.id1, NULL, worker_init, &runtime.worker_pool[i].ids.id2);
    }
}

// clean up the resource allcoated for the uthread
static void cleanup() {
    for (;;);
}

// allocate resources for a new thread and returns its id
uint32_t uthread_create(void *(*func)(void *), void *arg) {

    if (!runtime.is_active)
        runtime_init();

    struct node *newnode = malloc(sizeof(struct node));
    newnode->uthd.uc.uc_mcontext = malloc(sizeof(_STRUCT_MCONTEXT64));
    
    mcontext_t mc = newnode->uthd.uc.uc_mcontext;
    uint64_t *stack = (uint64_t *)((uint64_t)malloc(UTHREAD_STACK_SIZE) + UTHREAD_STACK_SIZE);
    *(--stack) = (uint64_t)cleanup;
    mc_set_rsp(mc, (uint64_t)(stack));
    mc_set_rdi(mc, (uint64_t)arg);
    mc_set_rip(mc, (uint64_t)func);
    newnode->uthd.id = runtime.next_uthread_id++;
    newnode->uthd.state = UTHREAD_STATE_SLEEPING;

    // uint32_t worker_id = runtime.next_worker_id;
    // struct worker *worker = &runtime.worker_pool[worker_id];
    struct worker *worker = &runtime.worker_pool[0];

    pthread_mutex_lock(&worker->mu);
    if (worker->qsize++ == 0) {
        worker->queue = newnode;
        newnode->next = newnode;
        newnode->prev = newnode;
    } else {
        newnode->next = worker->queue;
        newnode->prev = worker->queue->prev;
        worker->queue->prev->next = newnode;
        worker->queue->prev = newnode;
        worker->queue = newnode;
    }
    pthread_mutex_unlock(&worker->mu);

    // test
    asm("de:");
    pthread_kill(runtime.worker_pool[0].ids.id1, SIGUSR1);
}

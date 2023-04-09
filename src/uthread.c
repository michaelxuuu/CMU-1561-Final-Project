#include "uthread.h"

#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include "x86_64.h"

#define MAX_UTHREAD_COUNT 128
#define UTHREAD_ALIGNEMNT 16
#define UTHREAD_STACK_SIZE (sizeof(char) * 1024 * 1024 * 8)

struct uthread {
    uint32_t id;
    uint32_t state;
    uint32_t padding[2];
    ucontext_t uc;
};

// worker data
struct worker {
    pthread_mutex_t mu;
    struct uthread *queue;
    uint32_t qsize;
    uint32_t cur_pthread_id;
};
// worker ids (thread local)
static __thread pthread_t worker_id1;   // pthread id
static __thread uint32_t worker_id2;         // integer id (assigned by the run-time)

// run-time data
static struct {
    uint32_t is_active;
    struct worker *worker_pool;
    uint32_t pool_size;
    atomic_uint next_uthread_id;
    uint32_t *ids;
} runtime;

// uthread schedualer (installed as the SIGALARM handler)
static void* scheduler(uint32_t signo, siginfo_t *si, void *cpu_con) {
    
}

// initailze a worker (used on creation)
static void* worker_init(void *id) {
    worker_id1 = pthread_self();
    worker_id2 = *(uint32_t *)id;
    // busy waiting
    for (;;);
}

// initialize the run-time (invoked by the first call to uthread_create, used internally)
static void runtime_init() {
    runtime.is_active = 1;
    runtime.next_uthread_id = 0;
    // start as many cores as workers
    runtime.pool_size = (uint32_t)sysconf(_SC_NPROCESSORS_ONLN);
    runtime.ids = malloc(sizeof(uint32_t) * runtime.pool_size);
    runtime.worker_pool = malloc(sizeof(struct worker) * runtime.pool_size);

    for (size_t i = 0; i < runtime.pool_size; i++) {
        runtime.ids[i] = i;
        runtime.worker_pool[i].qsize = 0;
        runtime.worker_pool[i].queue = 0;
        pthread_mutex_init(&runtime.worker_pool[i].mu, 0);
        pthread_create(NULL, NULL, worker_init, &runtime.ids[i]);
    }
}

static void cleanup() {

}

// allocate resources for a new thread and returns its id
uint32_t uthread_create(void *(*func)(void *), void *arg) {

    if (!runtime.is_active)
        runtime_init();

    struct uthread *newthread = aligned_alloc(UTHREAD_ALIGNEMNT, sizeof(struct uthread));
    
    mcontext_t mc = newthread->uc.uc_mcontext;
    uint64_t *stack = malloc(UTHREAD_STACK_SIZE);
    stack[0] = (uint64_t)cleanup;
    mc_set_rsp(mc, (uint64_t)(stack + 1));
    mc_set_rdi(mc, (uint64_t)arg);
    mc_set_rip(mc, (uint64_t)func);
    newthread->id = runtime.next_uthread_id++;
}

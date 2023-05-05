#include "uthread.h"

#include <unistd.h>
#include <sys/syscall.h>
// mm.c
void *malloc(uint64_t size);
void free(void *ptr);
void *realloc(void *ptr, uint64_t size);
void *calloc(uint64_t nmemb, uint64_t size);

static uthread_mutex_t mu = UTHREAD_MUTEX_INITIALIZER;

void *umalloc(uint64_t size) {
    uthread_mutex_lock(&mu);
    void *ptr = malloc(size);
    if (!ptr) {
        uprintf("error: umalloc: \n");
        syscall(SYS_exit, 1);
    }
    uthread_mutex_unlock(&mu);
    return ptr;
}

void ufree(void *ptr) {
    uthread_mutex_lock(&mu);
    free(ptr);
    uthread_mutex_unlock(&mu);
}

// used by run-time only
void *_umalloc(uint64_t size) {
    void *ptr;
    for (;;) {
        uthread_mutex_lock(&mu);
        ptr = malloc(size);
        if (ptr) {
            break;
        } else {
            uthread_mutex_unlock(&mu); // release the lock so that free can aquire the lock
        }
    }

    uthread_mutex_unlock(&mu);
    return ptr;
}

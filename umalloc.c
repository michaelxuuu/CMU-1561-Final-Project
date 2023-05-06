#include "uthread.h"

#include <unistd.h>
#include <stdlib.h>
#include <sys/syscall.h>

static uthread_mutex_t mu = UTHREAD_MUTEX_INITIALIZER;

void *umalloc(uint64_t size) {
    uthread_mutex_lock(&mu);
    void *ptr = malloc(size);
    if (!ptr) {
        uprintf("error: umalloc: \n");
        exit(1);
    }
    uthread_mutex_unlock(&mu);
    return ptr;
}

void ufree(void *ptr) {
    uthread_mutex_lock(&mu);
    free(ptr);
    uthread_mutex_unlock(&mu);
}

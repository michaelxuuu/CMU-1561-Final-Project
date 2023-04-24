#define _UMALLOC_ // do not replace malloc by reentrant_malloc in this file

#include "uthread.h"

#include <stdlib.h>
#include <stdatomic.h>

atomic_uint malloc_count = 0;
atomic_uint free_count = 0;

uthread_mutex_t mu = UTHREAD_MUTEX_INITIALIZER;

void *reentrant_malloc(size_t size) {
    malloc_count++;
    void *r;
    uthread_mutex_lock(&mu);
    r = malloc(size);
    if (r == 0) {
        // __asm__("int $3");
        uprintf("error: reentrant_malloc: malloc:%d free:%d\n", malloc_count, free_count);
        exit(0);
    }
    uprintf("stat: malloc:%d free:%d\n", malloc_count, free_count);
    uthread_mutex_unlock(&mu);
    return r;
}

void reentrant_free(void *p) {
    free_count++;
    uthread_mutex_lock(&mu);
    free(p);
    uprintf("stat: malloc:%d free:%d\n", malloc_count, free_count);
    uthread_mutex_unlock(&mu);
}

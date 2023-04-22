#define _UMALLOC_ // do not replace malloc by reentrant_malloc in this file

#include "uthread.h"

#include <stdlib.h>

uthread_mutex_t mu = UTHREAD_MUTEX_INITIALIZER;

void *reentrant_malloc(size_t size) {
    void *r;
    uthread_mutex_lock(&mu);
    r = malloc(size);
    if (r == 0) {
        uprintf("error: reentrant_malloc\n");
        exit(0);
    }
    uthread_mutex_unlock(&mu);
    return r;
}

void reentrant_free(void *p) {
    uthread_mutex_lock(&mu);
    free(p);
    uthread_mutex_unlock(&mu);
}

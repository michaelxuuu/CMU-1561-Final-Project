#ifndef _uthread_h_
#define _uthread_h_

#define _GNU_SOURCE

#include <stdlib.h>

typedef unsigned long int uthread_t;
void uthread_create(uthread_t *id, void *(*func)(void *), void *arg);
int uthread_join(uthread_t, void *ret);

// sync.c
#define UTHREAD_MUTEX_INITIALIZER {0}
typedef struct {
    int locked;
} uthread_mutex_t;
void uthread_mutex_init(uthread_mutex_t *mu);
void uthread_mutex_lock(uthread_mutex_t *mu);
void uthread_mutex_unlock(uthread_mutex_t *mu);

// uprintf.c
void uprintf(const char *fmt, ...);

// umalloc.c
void *reentrant_malloc(size_t size);
void reentrant_free(void *ptr);
#ifndef _UMALLOC_
#define malloc(x) reentrant_malloc(x)
#define free(x) reentrant_free(x)
#endif

#endif

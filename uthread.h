#ifndef _uthread_h_
#define _uthread_h_

#define _GNU_SOURCE

#include <stdint.h>

typedef uint64_t uthread_t;
void uthread_create(uthread_t *id, void *(*func)(void *), void *arg);
int uthread_join(uthread_t, void **ret);
int uthread_detach(uthread_t id);

// sync.c
#define UTHREAD_MUTEX_INITIALIZER {0}
typedef struct { uint64_t locked; } uthread_mutex_t;
void uthread_mutex_init(uthread_mutex_t *mu);
void uthread_mutex_lock(uthread_mutex_t *mu);
void uthread_mutex_unlock(uthread_mutex_t *mu);

// uprintf.c
void uprintf(const char *fmt, ...);

// umalloc.c
void *umalloc(uint64_t size);
void ufree(void *ptr);

#endif

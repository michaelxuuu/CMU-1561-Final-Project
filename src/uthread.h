#ifndef _uthread_h_
#define _uthread_h_

#include <stdint.h>

typedef uint64_t uthread_t;

void uthread_create(uthread_t *id, void *(*func)(void *), void *arg);
int uthread_join(uthread_t, void *ret);

#endif

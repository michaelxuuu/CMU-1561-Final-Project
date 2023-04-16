#ifndef _uthread_h_
#define _uthread_h_

#define _GNU_SOURCE

typedef unsigned long int uthread_t;

void uthread_create(uthread_t *id, void *(*func)(void *), void *arg);
int uthread_join(uthread_t, void *ret);

#endif

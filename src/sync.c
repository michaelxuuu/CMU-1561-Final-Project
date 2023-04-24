#include <stdint.h>

#include "uthread.h"

// asm.s
extern int _cas32(void *ptr, int oldval, int newval);

void uthread_mutex_init(uthread_mutex_t *m) {
    m->locked = 0;
}

void uthread_mutex_lock(uthread_mutex_t *m) {
    while (_cas32(&m->locked, 0, 1) != 0);
}

void uthread_mutex_unlock(uthread_mutex_t *m) {
    m->locked = 0;
}


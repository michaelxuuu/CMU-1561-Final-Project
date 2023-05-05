#include <stdint.h>

#include "uthread.h"

// asm.s
extern uint32_t _cas(void *ptr, uint32_t oldval, uint32_t newval);

void uthread_mutex_init(uthread_mutex_t *m) {
    m->locked = 0;
}

void uthread_mutex_lock(uthread_mutex_t *m) {
    while (_cas(&m->locked, 0, 1) != 0);
}

void uthread_mutex_unlock(uthread_mutex_t *m) {
    m->locked = 0;
}

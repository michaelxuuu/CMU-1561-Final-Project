#ifndef _uthread_h_
#define _uthread_h_

#include <stdint.h>

uint32_t uthread_create(void *(*func)(void *), void *arg);

#endif

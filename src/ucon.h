#ifndef _ucon_h_
#define _ucon_h_

#include "x86_64.h"
#include <stdatomic.h>

// uthread context
struct uthread_context {
    uint32_t id;
    uint32_t state;
    ucontext_t uc;
    uint32_t marker;
    struct uthread_context *prev;
    struct uthread_context *next;
};

void ucon_insert(struct uthread_context *head, struct uthread_context *ucon);

#endif

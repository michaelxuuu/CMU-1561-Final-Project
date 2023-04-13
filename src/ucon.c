#include "ucon.h"
#include <stdatomic.h>
#include <stdint.h>

// compare the value stored at addr with the expected value
// if they equal, then write the update into the addr and return expected value
// otherwise return the discrepant value at addr
static inline uint64_t CAS(uint64_t addr, uint64_t expected, uint64_t update) {
    asm (
        "mov %rsi, %rax;"               // expected -> rax
        "lock cmpxchgq %rdx, (%rdi);"
    );
}

void ucon_insert(struct uthread_context *head, struct uthread_context *ucon) {

    struct uthread_context *old_head;
    do {
        old_head = head->next;
        ucon->prev = head;
        ucon->next = old_head;
    } while (CAS((uint64_t)&head->next, (uint64_t)old_head, (uint64_t)ucon) != (uint64_t)ucon->next);

    old_head->prev = ucon;
}

void ucon_delete(struct uthread_context *ucon) {

}

#include "ucon.h"
#include <stdatomic.h>
#include <stdint.h>


    // addr    -> rdi
    // old_val -> rsi
    // new_val -> rdx

    /*
        https://www.felixcloutier.com/x86/cmpxchg

        TEMP := DEST
        IF accumulator = TEMP
            THEN
                ZF := 1;
                DEST := SRC;
            ELSE
                ZF := 0;
                accumulator := TEMP;
                DEST := TEMP;
        FI;
    */
extern uint64_t CAS(uint64_t addr, uint64_t old_val, uint64_t new_val);

__asm__ (
    "_CAS:;"
    "mov %rsi, %rax;"
    "mov %rdx, %rsi;"
    "lock cmpxchgq %rsi, (%rdi);"
    "ret;"
);


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

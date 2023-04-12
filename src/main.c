
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "ucon.h"

struct uthread_context first_ucon;
struct uthread_context *head = &first_ucon;

int main(void) {
    first_ucon.id = 0;
    first_ucon.next = &first_ucon;
    first_ucon.prev = &first_ucon;
    struct uthread_context new;
    new.id = 1;
    ucon_insert(head, &new);
    asm("de:");
}


#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include "ucon.h"

struct uthread_context *head;

atomic_uint id = 0;

struct uthread_context *create() {
    struct uthread_context *ucon = malloc(sizeof(struct uthread_context));
    ucon->id = id++;
    ucon->prev = ucon;
    ucon->next = ucon;
    return ucon;
}

void *func(void *arg) {
    for (int i = 0; i < 100000; i++)
        ucon_insert(head, create());
}

pthread_t t[100];
int main(void) {
    head = create();
    for (int i = 0; i < 10; i++) {
        pthread_create(&t[i], 0, func, 0);
    }
    for (int i = 0; i < 10; i++) {
        pthread_join(t[i], 0);
    }
    printf("%d\n", head->id);
    for (struct uthread_context *p = head->next; p != head; p=p->next) {
        printf("%d\n", p->id);
    }
}

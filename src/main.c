#include "uthread.h"
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

void *func1(void *arg) {
    for (;;) {
        printf("func1\n");
    }
    return 0;
}

void *func2(void *arg) {
    for (;;) {
        printf("func2\n");
    }
    return 0;
}

int main() {
    uthread_create(func1, 0);
    uthread_create(func2, 0);
    for(;;) {
        // printf("main thread running\n");
        sleep(1);
    }
}

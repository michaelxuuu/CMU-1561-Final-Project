#include "uthread.h"
#include <stdio.h>

void *func(void *arg) {
    printf("asda\n");
    return 0;
}

int main() {
    uthread_create(func, 0);
    for(;;);
}


#include "uthread.h"

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

void *func1(void *none) {
    char *buf = "hello!\n";
    write(STDOUT_FILENO, buf, strlen(buf));
    return (void *)100;
}

void *func2(void *none) {
    for (;;);
    return 0;
}

int fib(int n) {
   if(n == 0){
      return 0;
   } else if(n == 1) {
      return 1;
   } else {
      return (fib(n-1) + fib(n-2));
   }
}

void *func3(void *none) {
    return (void *)(uintptr_t)fib(30);
}

int main(void) {
    int s = 100;
    uthread_t id[s];
    for (int i = 0; i < s; i++) {
        uthread_create(&id[i], func3, 0);
    }
    printf("done creation\n");
    for (int i = 0; i < s; i++) {
        void *r;
        if (!uthread_join(id[i], (void *)&r))
            printf("thread %ld exited, value returned: %ld\n", id[i], (uintptr_t)r);
    }
}


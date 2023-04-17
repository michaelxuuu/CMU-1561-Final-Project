
#include "../src/uthread.h"

#include <stdio.h>
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

long fib(long n) {
   if(n == 0){
      return 0;
   } else if(n == 1) {
      return 1;
   } else {
      return (fib(n-1) + fib(n-2));
   }
}

void *func3(void *none) {
    return (void *)fib(30);
}

int main(void) {
    int s = 100;
    uthread_t id[s];
    for (int i = 0; i < s; i++) {
        uthread_create(&id[i], func3, 0);
    }
    for (int i = 0; i < s; i++) {
        void *r;
        if (!uthread_join(id[i], (void *)&r))
            uprintf("thread %lu exited, value returned: %lu\n", id[i], (long)r);
    }
}


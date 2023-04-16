
#include "uthread.h"
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

void *func1(void *none) {
    // char *buf = "hello!\n";
    // write(STDOUT_FILENO, buf, strlen(buf));
    return (void *)100;
}

void *func2(void *none) {
    for (;;);
    return 0;
}

int main(void) {
    int s = 500;
    uthread_t id[s];
    for (int i = 0; i < s; i++) {
        uthread_create(&id[i], func1, 0);
    }
    printf("here\n");
    for (int i = 0; i < s; i++) {
        void *r;
        if (!uthread_join(id[i], (void *)&r))
            printf("thread %ld exited, value returned: %ld\n", id[i], (uintptr_t)r);
    }

}

// struct {
//     uint32_t a;
//     uint32_t b;
// } s = {0, 0};

// int main() {
//     uint32_t old[2] = {0, 0};
//     uint32_t new[2] = {0, 0};
//     DCAS((uint64_t)&s, old, new);
//     printf("%d %d\n", s.a, s.b);
// }

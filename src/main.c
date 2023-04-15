
#include "uthread.h"
#include <stdio.h>
#include <stdint.h>

void *func1(void *) {
    return (void *)100;
}

void *func2(void *) {
    for (;;);
}

int main(void) {
    int s = 2;
    uthread_t id[s];
    for (int i = 0; i < s; i++) {
        uthread_create(&id[i], func1, 0);
    }
    printf("here");
    for (int i = 0; i < s; i++) {
        void *r;
        if (!uthread_join(id[i], (void *)&r));
            // printf("thread %d exited, value returned: %d\n", id[i], r);
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

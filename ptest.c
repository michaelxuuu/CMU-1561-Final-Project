
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

void *func1(void *) {
    return (void *)100;
}

void *func2(void *) {
    for (;;);
}

int main(void) {
    int s = 10000;
    pthread_t id[s];
    for (int i = 0; i < s; i++) {
        pthread_create(&id[i], 0, func1, 0);
    }
    for (int i = 0; i < s; i++) {
        void *r;
        if (!pthread_join(id[i], (void *)&r));
            printf("thread %ld exited, value returned: %ld\n", id[i], (long)r);
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

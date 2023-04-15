#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>

#include <stdatomic.h>



void sigalarm_handler()  {
    printf("sig\n");
}

int main() {
    // set up timer
    struct sigaction sa;
    struct itimerval timer;

    // Configure the timer signal handler
    sa.sa_handler = sigalarm_handler;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGALRM);
    sigaddset(&sa.sa_mask, SIGUSR1);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);

    // Configure the timer to fire every 10ms 10000
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 1;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 1;

    // Start the timer
    setitimer(ITIMER_REAL, &timer, NULL);
    for(;;);
}

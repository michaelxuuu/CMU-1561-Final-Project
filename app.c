
#include "uthread.h"

#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/syscall.h>

typedef struct sockaddr sockaddr;
typedef struct sockaddr_in sockaddr_in;
typedef struct addrinfo addrinfo;

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
	// free(malloc(10));
      return (fib(n-1) + fib(n-2));
   }
}

void *func3(void *none) {
    return (void *)fib(30);
}

// int main(void) {
//     int n = atoi(getenv("UTHREADCT"));
//     uthread_t id[n];
//     for (int i = 0; i < n; i++) {
//         uthread_create(&id[i], func3, 0);
//     }
//     for (int i = 0; i < n; i++) {
//         void *r;
//         if (!uthread_join(id[i], (void *)&r));
//             // uprintf("thread %lu exited, value returned: %lu\n", id[i], (long)r);
//     }
// }

int make_listen_sock(uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        uprintf("socket(2): error: "), syscall(SYS_exit, 1);

    /* By having the sin_addr field of sockaddr_in
       to be all 0's, the ip address is effectively
       set to 0.0.0.0, a whildcard address, letting
       the socket to coonet to any ipv4 address */
    sockaddr_in myaddr;
    memset(&myaddr, 0, sizeof(myaddr));
    myaddr.sin_addr.s_addr = inet_addr("128.2.100.189");
    myaddr.sin_family = AF_INET;
    myaddr.sin_port = htons(port);

    /* bind the socket with the port passed in */
    int optval = 1; /* bool value for option - 0 or 1 */
    /* enable resue of the socket address (TCP port) of a recently-dead process
       right away without having to wait for 2 RTTs. This is a common practice
       for network server programs to have the porgram to be able to quickly
       reuse the myaddrme connection pair (local ip port; remote ip, port)
       without any delay */
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval,
               sizeof(int));

    /* to bind is just to tell the operating system to associate the file, which
       is a socket, with the address specified in a sockaddr struct, which is
       the address (port) that the calling process wishes to be listening on, so
       that later, when listen(2) is called, the operating system knows
       automatically, without passing in the address but the socket, which
       address should it be listening to */
    if (bind(sock, (sockaddr *)&myaddr, sizeof(myaddr)))
        uprintf("err: bind(2)\n"), syscall(SYS_exit, 1);

    /* set the socket as passive */
    if (listen(sock, 1024) < 0)
        uprintf("err: listen(2)\n"), syscall(SYS_exit, 1);

    return sock;
}

void *do_conn(void *connfd) {
    char buf[1024];
    for (;;) {
        int n = read((int)connfd, &buf, 1024);
        if (n == 0) break;
        write((int)connfd, buf, n);
    }
    return 0;
}

void check_heap(int flag);

int main() {
    int lnfd = make_listen_sock(8888);

    uthread_t ids[20];

    for (int ct = 0;; ct++) {
        sockaddr_in conn;
        socklen_t connlen = sizeof(conn);
        int connfd = accept(lnfd, (sockaddr *)&conn, &connlen);
        if (connfd == -1) 
            uprintf("err: accept(2)\n"), syscall(SYS_exit, 1);
        uprintf("accpeted conn %d\n", ct);
        uthread_create(&ids[0], do_conn, (void *)connfd);
        uthread_detach(ids[0]);
    }

    // for (int ct = 0; ct < 1; ct++)
    //     uthread_join(ids[ct], 0);

    // uthread_finalize();

    // check_heap(1);
}

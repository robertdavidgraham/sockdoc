/* simple-tcp-srv-echo.c
 An overly simplistic TCP server example with no error checking
 Example usage:
    simple-tcp-srv-echo 7777
 This will listen on port 7777, accept one connection at a time,
 and echo back whatever it receives on the connection.
 */
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

int main(int argc, char *argv[])
{
    const char *in_port = NULL;
    const char *in_address = NULL;
    struct addrinfo *ai = NULL;
    struct addrinfo hints = {0};
    int err;
    int fd = -1;

    if (argc < 2) {
        fprintf(stderr, "[-] usage: simple-tcp-srv-echo <port> [address]\n");
        return -1;
    }
    in_port = argv[1];
    if (argc >= 3)
        in_address = argv[2];
    
    hints.ai_flags = AI_PASSIVE;
    err = getaddrinfo(in_address, in_port, &hints, &ai);
    fd = socket(ai->ai_family, SOCK_STREAM, 0);
    err = bind(fd, ai->ai_addr, ai->ai_addrlen);
    err = listen(fd, 10);
    
    /* Loop accepting incoming connections */
    for (;;) {
        int fd2 = accept(fd, 0, 0);
        for (;;) {
            char buf[512];
            ssize_t count;

            count = recv(fd2, buf, sizeof(buf), 0);
            if (count <= 0)
                break;

            count = send(fd2, buf, count, 0);
            if (count < 0)
                break;
        } 
        close(fd2);
    }
    return 0;
}


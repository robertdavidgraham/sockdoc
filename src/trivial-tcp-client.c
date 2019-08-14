/* 
 trivial-tcp-client
 Simple example of writing a client program using TCP.
 Example usage:
    tcp-client www.google.com 80
 This will send an HTTP request, then dump the response it gets
 back from the server.
 */
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

static const char *my_http_request =
    "GET / HTTP/1.0\r\n"
    "User-Agent: trivial-tcp-client/0.0\r\n"
    "\r\n";

int main(int argc, char *argv[])
{
    struct addrinfo *ai = NULL;
    int fd = -1;
    ssize_t count;
    char buf[2048];
    
    if (argc != 3) {
        fprintf(stderr, "[-] usage: trivial-tcp-client <host> <port>\n");
        return -1;
    }
    
    getaddrinfo(argv[1], argv[2], 0, &ai);
    fd = socket(ai->ai_family, SOCK_STREAM, 0);
    connect(fd, ai->ai_addr, ai->ai_addrlen);
    send(fd, my_http_request, strlen(my_http_request), 0);
    count = recv(fd, buf, sizeof(buf), 0);
    printf("%.*s\n", (unsigned)count, buf);
    freeaddrinfo(ai);
    close(fd);
}


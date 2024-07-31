/* tcp-client
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

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/types.h>

/* useful when used with port 80 */
static const char *my_http_request = 
    "HEAD / HTTP/1.0\r\n"
    "User-Agent: tcp_client/0.0\r\n"
    "\r\n";

void
print_string(const unsigned char *buf, ssize_t count) {
    ssize_t i;
    if (count <= 0)
        return;
    for (i = 0; i < count; i++) {
            unsigned char c = buf[i];
            if (isprint(c) || isspace(c))
                printf("%c", c);
            else
                printf("."); /* non-printable characters */
        }
}

int
main(int argc, char *argv[])
{
    struct addrinfo *target = NULL;
    int err;
    int fd = -1;
    ssize_t count;
    unsigned char buf[65536];
    const char *hostname = argv[1];
    const char *portname = argv[2];

    if (argc != 3) {
        fprintf(stderr, "[-] usage:\n tcp-client <host> <port>\n");
        return -1;
    }

    err = getaddrinfo(hostname, portname, 0, &target);
    fd = socket(target->ai_family, SOCK_STREAM, 0);
    err = connect(fd, target->ai_addr, target->ai_addrlen);
    count = send(fd, my_http_request, strlen(my_http_request), 0);    
    count = recv(fd, &buf, sizeof(buf), 0);
    print_string(buf, count); 
    
    close(fd);
}

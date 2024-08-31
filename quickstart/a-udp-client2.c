/* a-udp-client2
 Simple example of writing a client program using UDP.
 Example usage:
    a-udp-client2 1.1.1.1 53
 This will send a DNS request, then dump the response it gets
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

/* useful when used with port 53 */
static const char my_dns_request[] =
"[\003\001 \000\001\000\000\000\000\000\001\003www\006google\003com\000\000\001\000\001\000\000)\020\000\000\000\000\000\000\000";

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
    printf("\n");
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
        fprintf(stderr, "[-] usage:\n a-udp-client <host> <port>\n");
        return -1;
    }

    err = getaddrinfo(hostname, portname, 0, &target);
    fd = socket(target->ai_family, SOCK_DGRAM, 0);
    count = sendto(fd, my_dns_request, sizeof(my_dns_request)-1, 0,
                    target->ai_addr, target->ai_addrlen);    
    count = recv(fd, &buf, sizeof(buf), 0);
    print_string(buf, count); 
    close(fd);
}

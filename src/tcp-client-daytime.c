/* tcp-client-daytime
 Simple client for the 'daytime' protocol.
 Example usage:
    tcp-client-daytime time-a-b.nist.gov
 This will connect and read a line of text which is probably a date.
 Reference: RFC 867
 */
#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

int main(int argc, char *argv[])
{
    struct addrinfo *ai;
    int err;
    int fd = -1;
    
    if (argc != 2) {
        fprintf(stderr, "[-] usage: tcp-client-daytime <host>\n");
        return -1;
    }
    
    /* Do a DNS lookup on the target/peer name */
    err = getaddrinfo(argv[1], "13", 0, &ai);
    if (err) {
        fprintf(stderr, "[-] getaddrinfo(): %s\n", gai_strerror(err));
        return -1;
    }
    
    /* Create a socket */
    fd = socket(ai->ai_family, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "[-] socket(): %s\n", strerror(errno));
        goto cleanup;
    }
    
    /* connect */
    err = connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (err) {
        fprintf(stderr, "[-] connect(): %s\n", strerror(errno));
        goto cleanup;
    }
    
    /* Now dump all the bytes in response until newline */
    for (;;) {
        char c;
        ssize_t count;
        
        count = recv(fd, &c, 1, 0);
        if (count == 0)
            break; /* opposite side closed connection */
        else if (count < 0) {
            fprintf(stderr, "recv(): %s\n", strerror(errno));
            break;
        }

        if (c == '\n')
            break; /* stop reading at end-of-line */
        else if (c == '\r')
            ; /* do nothing with it */
        else if (isprint(c) || isspace(c))
            printf("%c", c); /* print normal characters */
        else
            printf("."); /* don't print hostile characters */
    }
    
cleanup:
    if (ai)
        freeaddrinfo(ai);
    if (fd != -1)
        close(fd);
    return 0;
}


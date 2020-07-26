/* Probe RDP */
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
    err = getaddrinfo(argv[1], "3389", 0, &ai);
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
    
    /* Transmit */
    static const char payload[] = 
        "\x03\x00\x00\x2a"
        "\x25\xe0\x00\x00\x00\x00\x00\x43\x6f\x6f\x6b\x69\x65\x3a\x20\x6d" \
        "\x73\x74\x73\x68\x61\x73\x68\x3d\x6e\x6d\x61\x70\x0d\x0a\x01\x00" \
        "\x08\x00\x01\x00\x00\x00";
    static const char payload2[] = 
        "\x03\x00\x00\x2a"
        "\x25\xe0\x00\x00\x00\x00\x00\x43\x6f\x6f\x6b\x69\x65\x3a\x20\x6d" \
        "\x73\x74\x73\x68\x61\x73\x68\x3d\x6e\x6d\x61\x70\x0d\x0a\x01\x00" \
        "\x08\x00\x03\x00\x00\x00";

    send(fd, payload, sizeof(payload)-1, 0);
    char buf[256];
    recv(fd, buf, sizeof(buf), 0);
    send(fd, payload2, sizeof(payload2)-1, 0);



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


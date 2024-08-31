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

static const char *my_http_request = "HEAD / HTTP/1.0\r\n"
                                     "User-Agent: tcp_client/0.0\r\n"
                                     "\r\n";

int
main(int argc, char *argv[])
{
    struct addrinfo *targets = NULL;
    struct addrinfo *ai;
    int err;
    int fd = -1;
    ptrdiff_t count;
    const char *hostname;
    const char *portname;
    char addrstring[64];
    char portstring[8];
    struct sockaddr_storage localaddr;
    socklen_t localaddr_length = sizeof(localaddr);


    /* Ignore the send() problem */
    signal(SIGPIPE, SIG_IGN);

    if (argc != 3) {
        fprintf(stderr, "[-] usage: tcp-client <host> <port>\n");
        return -1;
    }
    hostname = argv[1];
    portname = argv[2];

    /* Do a DNS lookup on the name */
    err = getaddrinfo(hostname, portname, 0, &targets);
    if (err) {
        fprintf(stderr, "[-] getaddrinfo(): %s\n", gai_strerror(err));
        return -1;
    } else {
        count = 0;
        for (ai = targets; ai; ai = ai->ai_next)
            count++;
        if (count == 0) {
            fprintf(stderr, "[-] getaddrinfo(): returned zero targets\n");
            goto cleanup;
        } else {
            fprintf(stderr, "[+] getaddrinfo(): returned %d targets\n", (int)count);
        }
    }

    /* We'll just use the first result. Ideally, if the first address fails,
     * we should try the next address in the list. That's can exercise for
     * the student. */
    ai = targets;
    
    /* Print the address/port to strings for logging/debugging.
     * This is the reverse of getaddrinfo(). */
    err = getnameinfo(ai->ai_addr, ai->ai_addrlen, addrstring,
                      sizeof(addrstring), portstring, sizeof(portstring),
                      NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
        goto cleanup;
    }
    fprintf(stderr, "[ ] connecting TO [%s]:%s\n", addrstring, portstring);

    /* Create a socket, resources in the kernel */
    fd = socket(ai->ai_family, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "[-] socket(): %s\n", strerror(errno));
        goto cleanup;
    }
   
    struct timeval tv;
    tv.tv_sec = 5; /* five seconds */
    tv.tv_usec = 0;
    err = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (err) {
        fprintf(stderr, "[-] setsockopt(): %s\n", strerror(errno));
        goto cleanup;;
    }
    err = setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (err) {
        fprintf(stderr, "[-] setsockopt(): %s\n", strerror(errno));
        goto cleanup;;
    }

    /* Try to connect */
    err = connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (err) {
        fprintf(stderr, "[-] connect(): %s\n", strerror(errno));
        goto cleanup;
    } else {
        fprintf(stderr, "[+] connect(): %s\n", "succeeded");
    }
    
    /* Also log the address assigned to me on this side. */
    err = getsockname(fd, (struct sockaddr*)&localaddr, &localaddr_length);
    if (err) {
        fprintf(stderr, "[-] getsockname(): %s\n", strerror(errno));
    } else {
        err = getnameinfo((struct sockaddr*)&localaddr, localaddr_length,
                          addrstring, sizeof(addrstring),
                          portstring, sizeof(portstring),
                          NI_NUMERICHOST | NI_NUMERICSERV);
        if (err) {
            fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
        } else {
            fprintf(stderr, "[ ] connecting FROM [%s]:%s\n",
                    addrstring, portstring);
        }
    }


    /* The 'fd' socket now has a valid connection to the server, so send data */
    count = send(fd, my_http_request, strlen(my_http_request), 0);
    if (count == -1) {
        fprintf(stderr, "[-] send(): %s\n", strerror(errno));
        goto cleanup;
    }
    fprintf(stderr, "[+] send(): sent %d bytes\n", (int)count);

    /* Now dump all the bytes in response */
    fprintf(stderr, "[ ] recv(): receiving responses\n");
    for (;;) {
        unsigned char buf[1024];
        ssize_t i;
        count = read(fd, &buf, sizeof(buf));
        if (count == 0) {
            fprintf(stderr, "[+] recv(): connected ended cleanly\n");
            break; /* opposite side closed connection */
        }
        else if (count < 0) {
            fprintf(stderr, "[-] recv(): %s\n", strerror(errno));
            break;
        }
        fprintf(stderr, "[+] recv(): returned %d bytes\n", (int)count);

        for (i = 0; i < count; i++) {
            unsigned char c = buf[i];
            if (isprint(c) || isspace(c))
                printf("%c", c);
            else
                printf(".");
        }
    }
    
cleanup:
    freeaddrinfo(targets);
    if (fd != -1) {
        err = close(fd);
        if (err) {
            fprintf(stderr, "[-] close(): failed %s\n", strerror(errno));
        } else {
            fprintf(stderr, "[+] close(): success\n");
        }
    }
}

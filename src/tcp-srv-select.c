/* tcp-srv-select
 Example of using 'select()' for building a TCP server.
 This is an 'echo' server that echoes back whatever it receives.
 Example usage:
    tcp-srv-select 7777
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

//#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>


int main(int argc, char *argv[])
{
    struct addrinfo *ai = NULL;
    struct addrinfo hints = {0};
    int err;
    int fd = -1;
    int yes = 1;
    char hostaddr[NI_MAXHOST];
    char hostport[NI_MAXSERV];
    struct {char *buf; size_t len;} buffers[FD_SETSIZE];
    fd_set readset;
    fd_set writeset;
    fd_set errset;
    
    /* Ignore the send() problem */
    signal(SIGPIPE, SIG_IGN);

    if (argc < 2 || 3 < argc) {
        fprintf(stderr, "[-] usage: tcp-srv-one <port> [address]\n");
        return -1;
    }
    
    /* Get an address structure for the port */
    hints.ai_flags = AI_PASSIVE;
    err = getaddrinfo((argc==3)?argv[2]:0,  /* local address*/
                      argv[1],              /* local port number */
                      &hints,               /* hints */
                      &ai);                 /* result */
    if (err) {
        fprintf(stderr, "[-] getaddrinfo(): %s\n", gai_strerror(err));
        return -1;
    }

    /* And retrieve back again which addresses were assigned */
    err = getnameinfo(ai->ai_addr, ai->ai_addrlen,
                        hostaddr, sizeof(hostaddr),
                        hostport, sizeof(hostport),
                        NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
        goto cleanup;
    }
    
    /* Create a file handle for the kernel resources */
    fd = socket(ai->ai_family, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "[-] socket(): %s\n", strerror(errno));
        goto cleanup;
    }

    /* Allow multiple processes to share this IP address */
    err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (err) {
        fprintf(stderr, "[-] SO_REUSEADDR([%s]:%s): %s\n", hostaddr, hostport, strerror(errno));
        goto cleanup;
    }
    
#if defined(SO_REUSEPORT)
    /* Allow multiple processes to share this port */
    err = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
    if (err) {
        fprintf(stderr, "[-] SO_REUSEPORT([%s]:%s): %s\n", hostaddr, hostport, strerror(errno));
        goto cleanup;
    }
#endif

    /* Tell it to use the local port number (and optionally, address) */
    err = bind(fd, ai->ai_addr, ai->ai_addrlen);
    if (err) {
        fprintf(stderr, "[-] bind([%s]:%s): %s\n", hostaddr, hostport, strerror(errno));
        goto cleanup;
    }

    /* Configure the socket for listening (i.e. accepting incoming connections) */
    err = listen(fd, 10);
    if (err) {
        fprintf(stderr, "[-] listen([%s]:%s): %s\n", hostaddr, hostport, strerror(errno));
        goto cleanup;
    } else
        fprintf(stderr, "[+] listening on [%s]:%s\n", hostaddr, hostport);
    

    /* Zero the sets */
    FD_ZERO(&readset);
    FD_ZERO(&writeset);
    FD_ZERO(&errset);
    FD_SET(fd, &readset);
    FD_SET(fd, &writeset);
    FD_SET(fd, )

    /* Loop accepting incoming connections AND data on connections */
    for (;;) {
        struct timeval tv;
        fd_set readcheck;
        fd_set writecheck;
        fd_set errset;
        int nfds;
        size_t i;

        /* must be reset on every call, because select() can change it */
        tv.tv_sec = 0;
        tv.tv_usec = 100000; /* 100 milliseconds */

        /* initialize all the sets */
        FD_ZERO(&readset);
        FD_ZERO(&writeset);
        FD_ZERO(&errset);

        /* build the sets */
        for (i=0; i<connection_count; i++) {
            if (connections[0].is_write)
                FD_SET(connections)
        }

        fd_set 
        int fd2;
        struct sockaddr_in6 peer;
        socklen_t peer_addrlen = sizeof(peer);
        char peeraddr[NI_MAXHOST];
        char peerport[NI_MAXSERV];
    
        /* Wait until somebody connects to us */
        fd2 = accept(fd, sa, &peer_addrlen);
        if (fd2 == -1) {
            fprintf(stderr, "[-] accept([%s]:%s): %s\n", hostaddr, hostport, strerror(errno));
            continue;
        }

        /* Pretty print the incoming address/port */
        err = getnameinfo(sa, sa_addrlen,
                        peeraddr, sizeof(peeraddr),
                        peerport, sizeof(peerport),
                        NI_NUMERICHOST | NI_NUMERICSERV);
        if (err) {
            fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
            goto cleanup;
        }
        fprintf(stderr, "[+] accept([%s]:%s) from [%s]:%s\n", hostaddr, hostport, peeraddr, peerport);

        /* Loop on this connection receiving/transmitting data */
        for (;;) {
            char buf[512];
            ptrdiff_t bytes_received;
            ptrdiff_t bytes_sent;

            /* Wait until some bytes received or connection closed */
            bytes_received = recv(fd2, buf, sizeof(buf), 0);
            if (bytes_received == 0) {
                fprintf(stderr, "[+] close() from [%s]:%s\n", peeraddr, peerport);
                break;
            } else if (bytes_received == -1) {
                fprintf(stderr, "[-] error from [%s]:%s\n", peeraddr, peerport);
                break;
            } else
                fprintf(stderr, "[+] recv([%s]:%s) %d bytes\n", peeraddr, peerport, (int)bytes_received);
                

            /* Echo back to sender */
            bytes_sent = send(fd2, buf, bytes_received, 0);
            if (bytes_sent == -1) {
                fprintf(stderr, "[-] send([%s]:%s): %s\n", peeraddr, peerport, strerror(errno));
                break;
            } else
                fprintf(stderr, "[+] send([%s]:%s) %d bytes\n", peeraddr, peerport, (int)bytes_sent);
        }
     
        close(fd2);
    }

    
cleanup:
    if (fd > 0)
        close(fd);
    if (sa)
        free(sa);
    if (ai)
        freeaddrinfo(ai);
    return 0;
}


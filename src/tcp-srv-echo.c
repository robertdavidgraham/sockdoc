/* tcp-srv-one
 Simple example of recieving a TCP connection.
 This is an 'echo' server that echoes back whatever it receives.
 Example usage:
    tcp-srv-one 7777
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
    char localaddr[NI_MAXHOST];
    char localport[NI_MAXSERV];
    socklen_t sa_max;
    const char *portname = argv[1];
    const char *hostname = (argc>=3)?argv[2]:0;
        
    /* Ignore the send() problem */
    signal(SIGPIPE, SIG_IGN);

    if (argc < 2 || 3 < argc) {
        fprintf(stderr, "[-] usage: tcp-srv-one <port> [address]\n");
        return -1;
    }
    
    /* Get an address structure for the port */
    hints.ai_flags = AI_PASSIVE;
    err = getaddrinfo(hostname, portname, &hints, &ai);
    if (err) {
        fprintf(stderr, "[-] getaddrinfo(): %s\n", gai_strerror(err));
        return -1;
    }

    /* And retrieve back again which addresses were assigned */
    err = getnameinfo(ai->ai_addr, ai->ai_addrlen,
                        localaddr, sizeof(localaddr),
                        localport, sizeof(localport),
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

    /* Tell it to use the local port number (and optionally, address) */
    err = bind(fd, ai->ai_addr, ai->ai_addrlen);
    if (err) {
        fprintf(stderr, "[-] bind([%s]:%s): %s\n", 
                localaddr, localport, strerror(errno));
        goto cleanup;
    }

    /* Configure the socket for listening (i.e. accepting incoming connections).
     * The connection is now "half-open" at this point, with local address/port
     * specified, but with no remote address/port */
    err = listen(fd, 10);
    if (err) {
        fprintf(stderr, "[-] listen([%s]:%s): %s\n", 
                localaddr, localport, strerror(errno));
        goto cleanup;
    } else
        fprintf(stderr, "[+] listening on [%s]:%s\n", localaddr, localport);
    

    /* Loop accepting incoming connections */
    for (;;) {
        int fd2;
        struct sockaddr_storage remoteaddr;
        socklen_t remoteaddr_length = sizeof(remoteaddr);
        char hoststring[NI_MAXHOST];
        char portstring[NI_MAXSERV];
    
        /* Wait until somebody connects to us */
        fd2 = accept(fd, (struct sockaddr*)&remoteaddr, &remoteaddr_length);
        if (fd2 == -1) {
            fprintf(stderr, "[-] accept([%s]:%s): %s\n", 
                    localaddr, localport, strerror(errno));
            continue;
        }

        /* Pretty print the incoming address/port */
        err = getnameinfo((struct sockaddr*)&remoteaddr, remoteaddr_length,
                        hoststring, sizeof(hoststring),
                        portstring, sizeof(portstring),
                        NI_NUMERICHOST | NI_NUMERICSERV);
        if (err) {
            fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
            goto cleanup;
        }
        fprintf(stderr, "[+] accept([%s]:%s) from [%s]:%s\n", 
                localaddr, localport, hoststring, portstring);

        /* Loop on this connection receiving/transmitting data */
        for (;;) {
            char buf[1024];
            ptrdiff_t bytes_received;
            ptrdiff_t bytes_sent;

            /* Wait until some bytes received or connection closed */
            bytes_received = recv(fd2, buf, sizeof(buf), 0);
            if (bytes_received == 0) {
                fprintf(stderr, "[+] close() from [%s]:%s\n", 
                        hoststring, portstring);
                break;
            } else if (bytes_received == -1) {
                fprintf(stderr, "[-] error from [%s]:%s\n", 
                        hoststring, portstring);
                break;
            } else
                fprintf(stderr, "[+] recv([%s]:%s) %d bytes\n", 
                        hoststring, portstring, (int)bytes_received);
                

            /* Echo back to sender */
            bytes_sent = send(fd2, buf, bytes_received, 0);
            if (bytes_sent == -1) {
                fprintf(stderr, "[-] send([%s]:%s): %s\n", hoststring, portstring, strerror(errno));
                break;
            } else
                fprintf(stderr, "[+] send([%s]:%s) %d bytes\n", hoststring, portstring, (int)bytes_sent);
        }
     
        close(fd2);
    }

    
cleanup:
    if (fd > 0)
        close(fd);
    if (ai)
        freeaddrinfo(ai);
    return 0;
}


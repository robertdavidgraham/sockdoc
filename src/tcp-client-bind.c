/* tcp-client-bind
 Demonstrates using bind() to configure the local/source port number before
 doing a remote connection. Also demonstrates an error.
 Example usage:
    tcp-client-bind www.google.com 80 7777
 Parameters:
    targetname - target IP address or DNS name
    targetport - target port number to connect to
    localport - local port number to use
    localname - (optional) local IP address to use
  Yes, the order of these parameter is inconsistent/confusing.

  Note: This doesn't solve the problem of listing possible local
  IP addresses, which is a platform-specific problem.
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
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *local = NULL;
    struct addrinfo *target;
    int err;
    char targetaddr[64];
    char targetport[8];
    int i;
    struct {
        char *targetname;
        char *targetport;
        char *localport;
        char *localname;
        int ai_family;
    } cfg;
    
    if (argc != 4 && argc != 5) {
        fprintf(stderr, "[-] usage: tcp-client-bind <target> <targetport> <localport>\n");
        return -1;
    }

    /* Fetch the configuration from the command-line */
    cfg.targetname = argv[1];
    cfg.targetport = argv[2];
    cfg.localport = argv[3];
    if (argc == 5)
        cfg.localname = argv[4]; /* specific source IP address */
    else
        cfg.localname = 0; /* any local IP address */
    cfg.ai_family = AF_UNSPEC;

    /* If a local IP address was specified, then figure out which address family it
     * belongs to in order to restrict the results returned by a DNS lookup */
    if (cfg.localname) {
        err = getaddrinfo(cfg.localname, cfg.localport, 0, &local);
        if (err) {
            fprintf(stderr, "[-] getaddrinfo(%s): %s\n", cfg.localname, strerror(errno));
            exit(1);
        }
        cfg.ai_family = local->ai_family;
        freeaddrinfo(local);
        local = NULL;
    }
    switch (cfg.ai_family) {
    case AF_UNSPEC:
        fprintf(stderr, "[+] local address family = %s\n", "AF_UNSPEC");
        break;
    case AF_INET:
        fprintf(stderr, "[+] local address family = %s\n", "AF_INET v4");
        break;
    case AF_INET6:
        fprintf(stderr, "[+] local address family = %s\n", "AF_INET6");
        break;
    default:
        fprintf(stderr, "[+] local address family = %d\n", local->ai_family);
    }

    
    /* Ignore the send() problem */
    signal(SIGPIPE, SIG_IGN);

    /* Do a DNS lookup on the remote target */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = cfg.ai_family;
    err = getaddrinfo(cfg.targetname,       /* host name */
                      cfg.targetport,       /* port number */
                      &hints,               /* hints (defaults) */
                      &addresses);          /* results */
    if (err) {
        fprintf(stderr, "[-] getaddrinfo(): %s\n", gai_strerror(err));
        return -1;
    } else {
	    int count = 0;
	    for (target=addresses; target; target = target->ai_next)
	        count++;
	    fprintf(stderr, "[%s] getaddrinfo(): returned %d addresses\n", 
            count?"+":"-", count);
        if (count == 0)
            goto cleanup;
    }

    /* For simplicity, try only the first address, see tcp-client.c for trying
     * all the addresses. We want a single address to demonstrate what happens
     * when we try to establish two identical connections. */
    target = addresses;


    /* Do the same connection twice */
    for (i=0; i<2; i++) {
        int fd = -1;
        char localaddr[64];
        char localport[8];
        struct sockaddr_storage sa;
        socklen_t sa_len;
        int yes = 1;
    
        /* Print the address/port to strings for logging/debugging  */
        err = getnameinfo(target->ai_addr, target->ai_addrlen,
                            targetaddr, sizeof(targetaddr),
                            targetport, sizeof(targetport),
                            NI_NUMERICHOST | NI_NUMERICSERV);
        if (err) {
            fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
            goto cleanup;
        } else {
            fprintf(stderr, "[+] target = [%s]:%s\n", targetaddr, targetport);
        }
        
        /* Create a socket */
        fd = socket(target->ai_family, SOCK_STREAM, 0);
        if (fd == -1) {
            fprintf(stderr, "[-] socket(): %s\n", strerror(errno));
            goto cleanup;
        }

        /* Configure this socket to re-use the source port number. If you don't
         * do this, then on the second call to bind() you'll get an EADDRINUSE
         * error. This is in addition to the classic use of this option to
         * deal with TIMEDWAIT sockets. */
        err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (err) {
            fprintf(stderr, "[-] setsockopt(SO_REUSEADDR): %s\n", strerror(errno));
            exit(1);
        }

        /* Get an address structure for the local port. Must use "passive"
         * to get the "any" IP address (0.0.0.0, ::) rather than "localhost"
         * IP address (127.0.0.1, ::1) */
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = target->ai_family;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        err = getaddrinfo(cfg.localname, cfg.localport, &hints, &local);
        if (err) {
            fprintf(stderr, "[-] getaddrinfo(): %s\n", gai_strerror(err));
            return -1;
        }

        /* To pretty-print the local address/port that we've chosen */
        err = getnameinfo(local->ai_addr, local->ai_addrlen,
                            localaddr, sizeof(localaddr),
                            localport, sizeof(localport),
                            NI_NUMERICHOST | NI_NUMERICSERV);
        if (err) {
            fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
            goto cleanup;
        }
        
        /* bind the local address */
        err = bind(fd, local->ai_addr, local->ai_addrlen);
        if (err) {
            fprintf(stderr, "[-] bind([%s]:%s): %s\n",
                localaddr, localport, strerror(errno));
            exit(1);
        } else {
            fprintf(stderr, "[+] local address = [%s]:%s\n", localaddr, localport);
        }
        
        /* Try to connect */
        err = connect(fd, target->ai_addr, target->ai_addrlen);
        if (err) {
            fprintf(stderr, "[-] connect(): [%s]:%s -> [%s]:%s: %s\n", 
                localaddr, localport,
                targetaddr, targetport,
                strerror(errno));
            close(fd);
            exit(1);
        } else {
            fprintf(stderr, "[+] connect(): [%s]:%s -> [%s]:%s: %s\n", 
                localaddr, localport,
                targetaddr, targetport,
                "success");
        }

        /* Get the actual address being used*/
        sa_len = sizeof(sa);
        err = getsockname(fd, (struct sockaddr *)&sa, &sa_len);
        if (err) {
            fprintf(stderr, "[-] getsockname(): %s\n", strerror(errno));
            exit(1);
        }

        /* Repeat the same logic as above, which should now fill in the correct
         * source IP address */
        err = getnameinfo((struct sockaddr *)&sa, sa_len,
                            localaddr, sizeof(localaddr),
                            localport, sizeof(localport),
                            NI_NUMERICHOST | NI_NUMERICSERV);
        if (err) {
            fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
            goto cleanup;
        } else {
            fprintf(stderr, "[+] local address = [%s]:%s\n", localaddr, localport);
        }
        




    } /* end for loop */
    
cleanup:
    return 0;
}


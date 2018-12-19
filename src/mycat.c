/*
    This is a simple form of the famous "netcat" or "nc" program.
    It creates an outbound connection to a socket, allowing you
    to type text on that connection, or redirect a file into
    that connection.

    The purpose isn't to create a better "netcat", but to show the
    simple concepts involved.
*/
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h> /* fcntl(), F_GETFL, O_NONBLOCK */
#include <netinet/tcp.h>

/* Set with command-line '-d' otion to enable debug printf() */
int is_debug = 0;

/* Should we assume an incoming FIN means half-close? */
int is_halfclose = 0;

struct configuration
{
    const char *hostname;
    const char *portname;
};

enum {
    SOCKETS_NONBLOCKING     = 0x00000001,
    SOCKETS_BLOCKING        = 0x00000002,
    SOCKETS_IMEDIATE        = 0x00000004,
};

/**
 * Wraps the function for setting non-blocking for a socket, handling
 * portability issues.
 * @param fd
 *      A socket descriptor created with `socket()` or `accept()`.
 * @return
 *      0 on success, or -1 on error, with `errno` set to the error
 *      code explaining why.
 */
int
wrap_set_nonblocking(int fd)
{
	int flag;
	flag = fcntl(fd, F_GETFL, 0);
	flag |= O_NONBLOCK;
	flag = fcntl(fd, F_SETFL,  flag);
	if (flag == -1)
		return -1;
	else
		return 0;
}

/**
 * Wraps the function for turning on "keep-alives" for a TCP connection.
 * @param fd
 *      A socket descriptor created with `socket()` or `accept()`.
 * @param seconds
 *      If zero, this parameter is ignored.
 *      Otherwise, the number of seconds after a connection has been idle
 *      that keep-alives will be sent. The default for most systems is
 *      at least an hour, in our code, we might wnat one-second intervals.
 * @return
 *      0 on success, or -1 on error, with `errno` set to the error
 *      code explaining why.
 */
int
wrap_set_keepalive(int fd, int seconds)
{
    int yes = 1;
    int err;

    err = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
    if (err) {
        fprintf(stderr, "[-] setsockopt(SO_KEEPALIVE): %s\n", strerror(errno));
    }

    if (seconds) {
#ifdef TCP_KEEPALIVE
        err = setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &seconds, sizeof(seconds));
        if (err) {
            fprintf(stderr, "[-] setsockopt(TCP_KEEPALIVE): %s\n", strerror(errno));
        }
#endif
    }
    return err;
}

/**
 * Wraps the 'connect()' function, handling all the complicated bits for 
 * reliability and portability.
 * @param targetaddr
 *      An address or hostname. If an address like "192.168.1.103" or
 *      "2603:3001:1913:6000:f4a3:3a02:9adb:76cc", then this will simply
 *      be parsed. If a hostname, then this function will block doing
 *      a DNS lookup.
 * @param targetport
 *      A port number between 1 and 65535, inclusive. This is a string
 *      reprenseting the printed form of the number, rather than an
 *      integer.
 * @return
 *      A valid socket descriptor, or -1 on error, in which case `errno`
 *      is set to error condition.
 */
static int
wrap_connect(const char *targetaddr, const char *targetport, int flags)
{
    int fd = -1;
    int err;
    struct addrinfo *ai;;
    
    /* Do a DNS lookup on the name, or parse the IP address.  */
    err = getaddrinfo(targetaddr, targetport, 0, &ai);
    if (err) {
        fprintf(stderr, "[-] getaddrinfo([%s]:%s): %s\n", 
                targetaddr, targetport, gai_strerror(err));
        return -1;
    }

    /* Create a socket */
    fd = socket(ai->ai_family, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "[-] socket(): %s\n", strerror(errno));
        goto error_cleanup;
    }

    /* Set to non-blocking mode, if necessary */
    if (flags & SOCKETS_NONBLOCKING)
        wrap_set_nonblocking(fd);
    
    /* Try to connect */
    err = connect(fd, ai->ai_addr, ai->ai_addrlen);

    /* If non-blocking or interrupted by a signal, then we have to poll
     * for a response */
    if (err && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EALREADY || errno == EINTR)) {
        
        /* If 'immediate', then the caller expects to use poll()/select() to
         * wait for a result instead of blocking in this function */
        if (flags & SOCKETS_IMEDIATE)
            return fd;

        fd_set writeset;
        fd_set errset;
        FD_ZERO(&writeset);
        FD_ZERO(&errset);
        FD_SET(fd, &writeset);
        FD_SET(fd, &errset);
        err = select(fd + 1, 0, &writeset, &errset, 0);
        if (err < 0) {
            fprintf(stderr, "[-] select(): %s\n", strerror(errno));
            goto error_cleanup;
        }
        if (FD_ISSET(fd, &errset)) {
            int errcode = 0;
            socklen_t sizeof_errcode = sizeof(errcode);
            err = getsockopt(fd, SOL_SOCKET, SO_ERROR, &errcode, &sizeof_errcode);
            if (err) {
                fprintf(stderr, "[-] select(): %s\n", strerror(errno));
                goto error_cleanup;
            } else {
                fprintf(stderr, "[-] connect(): %s\n", strerror(errno));
                goto error_cleanup;
            }
        }
    } else if (err) {
        fprintf(stderr, "[-] connect([%s]:%s): %s\n", targetaddr, targetport, strerror(errno));
        goto error_cleanup;
    }
    return fd;

error_cleanup:
    if (fd != -1)
        close(fd);
    return -1;
}

/**
 * Called when a configuration error occurs to print usage information
 */
static void
print_usage_and_exit(void)
{
    fprintf(stderr, "usage: mycat <hostname> <port> [<options> ...]\n");
    exit(1);
}

/**
 * Parse the command-line parameters
 */
struct configuration
parse_command_line(int argc, char *argv[])
{
    int i;
    struct configuration config = {0};
    
    if (argc == 1)
        print_usage_and_exit();
    
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
                case '?':
                case 'h':
                    /* The "-h" and "-?" are command for getting help */
                    print_usage_and_exit();
                    break;
                case 'd':
                    is_debug++;
                    break;
                case 'K':
                    is_halfclose = 1;
                    break;
                case '-':
                    if (strcmp(argv[i], "--help") == 0)
                        print_usage_and_exit();
                    break;
            }
        } else if (0 < atoi(argv[i]) && atoi(argv[i]) < 65535) {
            if (config.portname) {
                fprintf(stderr, "[-] unknown option: %s (port=%s)\n", argv[i], config.portname);
                exit(1);
            }
            config.portname = argv[i];
        } else if (strchr(argv[i], '.')) {
            if (config.hostname) {
                fprintf(stderr, "[-] unknown option: %s (target=%s)\n", argv[i], config.hostname);
                exit(1);
            }
            config.hostname = argv[i];
        } else {
            fprintf(stderr, "[-] unknown option: %s\n", argv[i]);
        }
    }

    return config;
}

/**
 * Handle an error on the socket
 */
int
handle_error(int fd)
{
    int err;
    int errcode = 0;
    socklen_t sizeof_errcode = sizeof(errcode);
    
    err = getsockopt(fd, SOL_SOCKET, SO_ERROR, &errcode, &sizeof_errcode);
    if (err) {
        fprintf(stderr, "[-] getsockopt(): %s\n", strerror(errno));
        return -1;
    }

    fprintf(stderr, "[-] connection error: %s\n", strerror(errno));

    return -1;
}


/**
 * The main loop, processing data from either end out the other
 */
int
main_loop(int fd, int fdin, int fdout)
{
    bool is_receiving = true;

    for (;;) {
        fd_set readset;
        fd_set writeset;
        fd_set errset;
        int nfds = 0;
        int err;

        /* Clear the descriptor sets */
        FD_ZERO(&readset);
        FD_ZERO(&writeset);
        FD_ZERO(&errset);

        /* Add our descriptors */
        if (is_receiving) {
            FD_SET(fd, &readset);
        }
        FD_SET(fd, &errset);
        if (nfds < fd)
            nfds = fd;
    
        if (fdin != -1) {
            FD_SET(fdin, &readset);
            FD_SET(fdin, &errset);
            if (nfds < fdin)
                nfds = fdin;
        }

        /* Wait for input from either end */
        err = select(nfds + 1, &readset, &writeset, &errset, 0);
        
        /* If there was an error, then exit out of this loop. This shouldn't
         * be possible, except due to programming bug */
        if (err < 0) {
            fprintf(stderr, "[-] select(): %s\n", strerror(errno));
            return -1;
        }

        /* If a timeout occurred, then just loop around and try again */
        if (err == 0) {
            continue;
        }

        /* Handle errors */
        if (FD_ISSET(fd, &errset))
            return handle_error(fd);
        if (FD_ISSET(fdin, &errset))
            return handle_error(fdin);
           
        /* Handle read events */
        if (FD_ISSET(fd, &readset)) {
            char buf[512];
            ssize_t count;

            /* Try to read bytes from the connection */
            count = recv(fd, buf, sizeof(buf), 0);
            if (count < 0) {
                fprintf(stderr, "[-] recv(): %s\n", strerror(errno));
                return -1;
            }
            if (is_debug)
                fprintf(stderr, "[+] %d-bytes from peer\n", (int)count);

            /* If we get zero '0', then that means the remote connection has
             * closed it's write end */
            if (count == 0) {
                if (is_halfclose) {
                    /* Since it's closed it's write-end, then we should close our read-end */
                    shutdown(fd, SHUT_RD);

                    /* Stop trying to read from this connection */
                    //is_receiving = false;
                    
                    /* It may have also done a full-close, closing it's
                    * read-end. We won't discover this until we write to the
                    * socket. Therefore, enable keep-alives, which occasionally
                    * write zero bytes, which will probe for a closed
                    * connection */
                    wrap_set_keepalive(fd, 1);
                } else {
                    /* The connection has been closed, so close the socket */
                    close(fd);
                    return 0;
                }
            }

            /* Send the bytes to the other connection */
            if (count > 0) {
                ssize_t count2;

                count2 = write(fdout, buf, count);
                if (count2 < 0) {
                    fprintf(stderr, "[-] send(stdout): %s\n", strerror(errno));
                    return -1;
                }
            }
        }
        
        /* read from <stdin> */
        if (FD_ISSET(fdin, &readset)) {
            char buf[512];
            ssize_t count;

            /* Try to read bytes from the stding */
            count = read(fdin, buf, sizeof(buf));
            if (count < 0) {
                fprintf(stderr, "[-] recv(stdin): %s\n", strerror(errno));
                return -1;
            }
            if (is_debug)
                fprintf(stderr, "[+] %d-bytes from stdin\n", (int)count);

            /* If we get zero '0', that means we are done reading input from
             * the command-line */
            if (count == 0) {
                /* We are done sending data to the server, but we still need
                 * to read data, so shutdown the write-end only */
                shutdown(fd, SHUT_WR);

                /* Set the file descriptor to -1 so that we'll stop adding it
                 * to our select() list */
                fdin = -1;
            }

            /* Send the bytes to the other connection */
            if (count > 0) {
                ssize_t count2;

                count2 = send(fd, buf, count, 0);
                if (count2 < 0) {
                    fprintf(stderr, "[-] send(fd): %s\n", strerror(errno));
                    return -1;
                }
            }
        }
    }

    return 0;
}

int 
log_connection(int fd)
{
    char hostaddr[64];
    char hostport[16];
    char peeraddr[64];
    char peerport[16];
    struct sockaddr_storage sa;
    socklen_t sa_addrlen = sizeof(sa);
    int err;

    /* Get our side */
    err = getsockname(fd, (struct sockaddr *)&sa, &sa_addrlen);
    if (err) {
        fprintf(stderr, "[-] getpeername(): %s\n", strerror(errno));
        return -1;
    }

    /* Format our side */
    err = getnameinfo((struct sockaddr *)&sa, sa_addrlen,
                    hostaddr, sizeof(hostaddr),
                    hostport, sizeof(hostport),
                    NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
        return -1;
    }


    /* Get the other side */
    err = getpeername(fd, (struct sockaddr *)&sa, &sa_addrlen);
    if (err) {
        fprintf(stderr, "[-] getpeername(): %s\n", strerror(errno));
        return -1;
    }

    /* Format other side */
    err = getnameinfo((struct sockaddr *)&sa, sa_addrlen,
                    peeraddr, sizeof(peeraddr),
                    peerport, sizeof(peerport),
                    NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
        return -1;
    }

    /* Log the connection */
    fprintf(stderr, "[+] connected from [%s]:%s to [%s]:%s\n", 
            hostaddr, hostport, peeraddr, peerport);
    return 0;
}

int
main(int argc, char *argv[])
{
    int fd;
    struct configuration config;
    int err;

    /* Disable the SIGGPIPE bug */
    signal(SIGPIPE, SIG_IGN);

    /* Read in the configuration */
    config = parse_command_line(argc, argv);
    if (config.hostname == NULL) {
        fprintf(stderr, "[-] hostname must be specified\n");
        exit(1);
    }
    if (config.portname == NULL) {
        fprintf(stderr, "[-] portname must be specified\n");
        exit(1);
    }

    /* Connect to the target */
    fd = wrap_connect(config.hostname, config.portname, 0);
    if (fd < 0) {
        fprintf(stderr, "[-] could not connect\n");
        exit(1);
    }
    
    /* Log this connection, if debugging enabled */
    if (is_debug)
        log_connection(fd);

    /* Now sit in the main loop processing data */
    err = main_loop(fd, STDIN_FILENO, STDOUT_FILENO);

    if (is_debug)
        fprintf(stderr, "[+] done %d\n", err);
    return 0;
}

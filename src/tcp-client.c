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
#include <time.h>
#include <fcntl.h>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>

static const char *my_http_request = "HEAD / HTTP/1.0\r\n"
                                     "User-Agent: tcp_client/0.0\r\n"
                                     "\r\n";

static int
LOG_connecting_to(struct sockaddr *addr, socklen_t addrlen) {
    char addrstring[64];
    char portstring[8];
    int err;

    err = getnameinfo(addr, addrlen, addrstring,
                      sizeof(addrstring), portstring, sizeof(portstring),
                      NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
        goto cleanup;
    }
    fprintf(stderr, "[ ] connecting TO [%s]:%s\n", addrstring, portstring);
    return 0;
cleanup:
    return 1;
}
static int
LOG_connecting_from(int fd) {
    int err;
    char addrstring[64];
    char portstring[8];
    struct sockaddr_storage localaddr;
    socklen_t localaddr_length = sizeof(localaddr);

    err = getsockname(fd, (struct sockaddr*)&localaddr, &localaddr_length);
    if (err) {
        fprintf(stderr, "[-] getsockname(): %s\n", strerror(errno));
        return err;
    }

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

    return 0;
}

static int
my_set_nonblocking(int fd) {
    int err;
    
    /* Configure the socket to be non-blocking. This can be done either
     * using `ioctl()` or `fcntl()`. */
#ifdef xFIONBIO
    int flags = 1;
    err = ioctl(fd, FIONBIO, &flags);
    if (err == -1) {
        fprintf(stderr, "[-] ioctl(FIONBIO) failed: %s\n", strerror(errno));
        goto cleanup;
    }
#elif defined(F_GETFL)
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        fprintf(stderr, "[-] fcntl(F_GETFL) failed: %s\n", strerror(errno));
        goto cleanup;
    }    
    flags |= O_NONBLOCK;
    err = fcntl(fd, F_SETFL, flags);
    if (err == -1) {
        fprintf(stderr, "[-] fcntl(F_SETFL) failed: %s\n", strerror(errno));
        goto cleanup;
    }
#endif
    return 0;
cleanup:
    return -1;
}
static int 
my_set_blocking(int fd, unsigned timeout_in_seconds) {
    int err;
    
    /* Configure the socket to be non-blocking. This can be done either
     * using `ioctl()` or `fcntl()`. */
#ifdef xFIONBIO
    int flags = 0;
    err = ioctl(fd, FIONBIO, &flags);
    if (err == -1) {
        fprintf(stderr, "[-] ioctl(FIONBIO) failed: %s\n", strerror(errno));
        goto cleanup;
    }
#elif defined(F_GETFL)
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        fprintf(stderr, "[-] fcntl(F_GETFL) failed: %s\n", strerror(errno));
        goto cleanup;
    }    
    flags &= ~O_NONBLOCK;
    err = fcntl(fd, F_SETFL, flags);
    if (err == -1) {
        fprintf(stderr, "[-] fcntl(F_SETFL) failed: %s\n", strerror(errno));
        goto cleanup;
    }
#endif

    /* Now set a timeout */
    struct timeval tv;
    tv.tv_sec = timeout_in_seconds;
    tv.tv_usec = 0;
    err = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (err) {
        fprintf(stderr, "[-] setsockopt(): %s\n", strerror(errno));
        goto cleanup;;
    }

    return 0;
cleanup:
    return -1;
}


static int
my_connect(int fd, struct sockaddr *addr, socklen_t addrlen, unsigned timeout_in_seconds) {
    time_t start = time(0); /* for measuring elapsed connetion time */
    int err;

    /* We are going to "block" using the `select()` funtion instead of relying upon
     * the `connect()` function to block. Therefore, we configure the socket to
     * be non-blocking. After `connect()` succeeds, we put it back to blocking mode */
    my_set_nonblocking(fd);

    /* This is where we do the most important part of this function, initiating the 
     * actual connection. This may take some time, but the function returns immediately
     * while the kernel continues the connection process invisible to us. */
    err = connect(fd, addr, addrlen);

    /* If we conned to another process on the localhost (127.0.0.1), then
     * there is no need to block, and some operating-systems will return
     * with a successfully completed connection. In this case, we skip
     * using `select()` */
    if (err == 0) {
        goto success;
    }

    /* These are all the errors returned by `connect()` for which we need to use
     * select() to wait for a response. All the other errors are simply errors
     * telling us we won't be aboe to complete the connection */
    switch (errno) {
#if EAGAIN != EWOULDBLOCK
        /* some systems, these are the same, others, they aren't, but we need
         * to catch both. If they are the same, then they can't be two case
         * statements with their values. */
        case EWOULDBLOCK:
#endif
        case EINPROGRESS: /* non-blocking socket */
        case EINTR: /* interrupted by signal */
        case EAGAIN: /* timed out */
            fprintf(stderr, "connect = %d (%d) %s\n", err, errno, strerror(errno));
            break;
        default:
            fprintf(stderr, "[+] connect() returned %d: %s\n", errno, strerror(errno));
            goto fail;
    }
 
    fd_set writefds;
    struct timeval timeout;
    int count;

try_again:
    FD_ZERO(&writefds);
    FD_SET(fd, &writefds);
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    
    /* */
    count = select(fd+1, NULL, &writefds, NULL, &timeout);
fprintf(stderr, "count = %d\n", count);
    /* Select should never return an error */
    if (count == -1) {
        if (errno == EAGAIN || errno == EINTR) {
            /* Treat these as a successful return instead,
             * so the select() will try again. */
            count = 0;
        } else {
            fprintf(stderr, "[-] select(): %s\n", strerror(errno));
            return err;
        }
    }

    /* Handle when there is a timeout or interrupted call */
    if (count == 0) {
        time_t now = time(0);
        if (now > start + timeout_in_seconds) {
            errno = ETIMEDOUT;
            return -1;
        } else
            goto try_again;
    }

    if (FD_ISSET(fd, &writefds) == 0) {
        /* This should be impossible, if */
        fprintf(stderr, "[-] select(): impossible condition\n");
        return 0;
    }

    int so_error = 0;
    socklen_t so_length = sizeof(so_error);
    err = getsockopt(fd, SOL_SOCKET, SO_ERROR, (void*)&so_error, &so_length);
    if (err) {
        fprintf(stderr, "[-] getsockopt(SO_ERROR): %s\n", strerror(errno));
        goto fail;
    }

    if (so_error == 0) {
        goto success;
    } else {
        errno = so_error;
        goto fail;
    }

success:
    my_set_blocking(fd, timeout_in_seconds);
    return 0; /* successfully connected */

fail:
    return -1;
}



static int
my_get_target(const char *hostname, const char *portname, struct sockaddr_storage *addr, socklen_t *addrlen) {
    int err;
    struct addrinfo *targets = NULL;
    struct addrinfo *ai;

    /* Do a DNS lookup on the name */
    err = getaddrinfo(hostname, portname, 0, &targets);
    if (err) {
        fprintf(stderr, "[-] getaddrinfo(): %s\n", gai_strerror(err));
        return err;
    }

    size_t count = 0;
    for (ai = targets; ai; ai = ai->ai_next)
        count++;
    if (count == 0) {
        fprintf(stderr, "[-] getaddrinfo(): returned zero targets\n");
        return -1;
    } else {
        fprintf(stderr, "[+] getaddrinfo(): returned %d targets\n", (int)count);
    }

    if (targets->ai_addrlen < sizeof(*addr))
        memcpy(addr, targets->ai_addr, targets->ai_addrlen);
    if (addrlen)
        *addrlen = targets->ai_addrlen;
    freeaddrinfo(targets);
    return 0;
}

static int
my_send_all(int fd, const char *buf, size_t sizeof_buf, int flags) {
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
    int count;
    size_t remaining = sizeof_buf;
    int loop_count = 0;
again:
    count = send(fd, buf, remaining, flags | MSG_NOSIGNAL);
    if (count < 0)
        return count; /* err */
    if (count < remaining) {
        buf += count;
        remaining -= count;
        if (loop_count++ < sizeof_buf) {
            goto again;
        } else {
            fprintf(stderr, "[ ] my_send_all(): loop exceed\n");
            errno = EFAULT;
            return -1;
        }
    }
    return sizeof_buf;
}

int
main(int argc, char *argv[])
{
    int err;
    int fd = -1;
    ptrdiff_t count;
    const char *hostname = argv[1];
    const char *portname = argv[2];
    struct sockaddr_storage target;
    socklen_t target_size = sizeof(target);

    /* Ignore the send() problem */
    signal(SIGPIPE, SIG_IGN);

    if (argc != 3) {
        const char  *progname = argv[0];
        fprintf(stderr, "[-] usage: %s <host> <port>\n", progname);
        return -1;
    }

    /*
     * Uses `getaddrinfo()` to parse the hostname and do a DNS
     * lookup if necessary.
     */
    err = my_get_target(hostname, portname, &target, &target_size);
    if (err)
        goto cleanup;

    /* 
     * Create a socket, reserving resources in the kernel. At this point in the code , the
     * socket is not configured with any address/ports, or have any options set,
     * other than the fact that TCP is being used, and either IPv4 or IPv6, depending
     * upon what was returned by DNS.
     */
    fd = socket(target.ss_family, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "[-] socket(): %s\n", strerror(errno));
        goto cleanup;
    }

    /* 
     * 
     */
    time_t start = time(0);
    LOG_connecting_to((struct sockaddr *)&target, target_size);
    err = my_connect(fd, (struct sockaddr*)&target, target_size, 5);
    if (err) {
        fprintf(stderr, "[-] connect(): %s\n", strerror(errno));
        goto cleanup;
    }
    LOG_connecting_from(fd);
    time_t now = time(0);
    fprintf(stderr, "[+] connection succeeded in = %d seconds\n", (int)(now - start));

    err = my_send_all(fd, my_http_request, strlen(my_http_request), 0);
    if (err) {
        fprintf(stderr, "[-] send(): %s\n", strerror(errno));
        goto cleanup;
    }
    fprintf(stderr, "[+] send(): sent %d bytes\n", (int)strlen(my_http_request));

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
    if (fd != -1) {
        err = close(fd);
        if (err) {
            fprintf(stderr, "[-] close(): failed %s\n", strerror(errno));
        } else {
            fprintf(stderr, "[+] close(): success\n");
        }
    }
}

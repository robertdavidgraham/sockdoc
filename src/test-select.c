/*
    This program tests `select()`, running it through various tests
    to make sure that it behaves correctly.
*/
#include <errno.h>
#include <stdio.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h> /* getaddrinfo() et al. */
#include <fcntl.h> /* fcntl(), F_GETFL, O_NONBLOCK */


struct connection_t
{
    int fd;
    char addr[64];
    char port[16];
};

/**
 * Set up a server listening on a random port
 */
static int
setup_random_server(struct connection_t *result)
{
    struct addrinfo *ai = NULL;
    struct addrinfo hints = {0};
    int err;
    int fd = -1;
    int yes = 1;
    struct sockaddr *sa = NULL;
    socklen_t sa_max;

    /* Doesn't matter if this is truly random or not */
    srand(time(0));
    
    /* Select a random port */
    unsigned port = rand() % (65536 - 2048) + 2048;
    snprintf(result->port, sizeof(result->port), "%u", port);

    /* Get an address structure for the local port */
    hints.ai_flags = AI_PASSIVE;
    err = getaddrinfo(0,                    /* any local address*/
                      result->port,     /* local port number */
                      &hints,               /* hints */
                      &ai);                 /* result */
    if (err) {
        fprintf(stderr, "[-] getaddrinfo(): %s\n", gai_strerror(err));
        return -1;
    }

    /* And retrieve back again which addresses were assigned, so that
     * we can print helpful messages below */
    err = getnameinfo(ai->ai_addr, ai->ai_addrlen,
                        result->addr, sizeof(result->addr),
                        result->port, sizeof(result->port),
                        NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
        goto error_cleanup;
    }
    
    /* Create a file handle for the kernel resources */
    fd = socket(ai->ai_family, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "[-] socket(): %s\n", strerror(errno));
        goto error_cleanup;
    }

    /* Allow multiple processes to share this IP address */
    err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (err) {
        fprintf(stderr, "[-] SO_REUSEADDR([%s]:%s): %s\n", 
                result->addr, result->port, strerror(errno));
        goto error_cleanup;
    }
    
#if defined(SO_REUSEPORT)
    /* Allow multiple processes to share this port */
    err = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
    if (err) {
        fprintf(stderr, "[-] SO_REUSEPORT([%s]:%s): %s\n", 
                result->addr, result->port, strerror(errno));
        goto error_cleanup;
    }
#endif

    /* Tell it to use the local port number (and optionally, address) */
    err = bind(fd, ai->ai_addr, ai->ai_addrlen);
    if (err) {
        fprintf(stderr, "[-] bind([%s]:%s): %s\n", 
                result->addr, result->port, strerror(errno));
        goto error_cleanup;
    }

    /* Configure the socket for listening (i.e. accepting incoming connections) */
    err = listen(fd, 10);
    if (err) {
        fprintf(stderr, "[-] listen([%s]:%s): %s\n",
                result->addr, result->port, strerror(errno));
        goto error_cleanup;
    }

    result->fd = fd;
    return 0;

error_cleanup:
    return 1;
}

/**
 * Configures the socket for non-blocking status
 */
int wrap_set_nonblocking(int fd)
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

int
wrap_connect(const char *targetaddr, const char *targetport)
{
    int fd = -1;
    int err;
    struct addrinfo *ai;;
    
    /* Do a DNS lookup on the name */
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

    wrap_set_nonblocking(fd);
    
    /* Try to connect */
    err = connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (err && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EALREADY || errno == EINTR)) {
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


int
test_client_abort(const struct connection_t *srvr)
{

    return 1;
}

int main(int argc, char *argv[])
{
    int err;
    struct connection_t srvr = {0};

    /* Ignore the send() problem */
    signal(SIGPIPE, SIG_IGN);

    if (argc > 1 && strcmp(argv[1], "client")) {
        return  1;
    }

    /* Set up a server */
    err = setup_random_server(&srvr);
    if (err) {
        fprintf(stderr, "[-] failed, couldn't setup listening server\n");
        return 1;
    } else {
        fprintf(stderr, "[+] listening on [%s]:%s\n", 
                srvr.addr, srvr.port);
    }


    pid_t pid;

    pid = fork();

    /* If child/client, send bad data and return */
    if (pid == 0) {
        int fd;
        int count;
        char buf[512];

        memset(buf, ' ', sizeof(buf));
        
        fprintf(stderr, "[ ] connecting to: [%s]:%s ...\n", srvr.addr, srvr.port);
        fd = wrap_connect(srvr.addr, srvr.port);
        fprintf(stderr, "[+] connected to: [%s]:%s\n", srvr.addr, srvr.port);
        for (;;) {
            int count; 
            count = send(fd, buf, sizeof(buf), 0);
            //fprintf(stderr, "[+] send() to [%s]:%s with %d bytes\n", srvr.addr, srvr.port, count);
            if (count < 0)
                break;
            if (count < sizeof(buf))
                break;
        }

        close(fd);
        return 0;
    }
    
    struct connection_t peer;
    struct sockaddr_storage sa;
    socklen_t sa_addrlen = sizeof(sa);
    

    /* Wait for an incoming connection */
    for (;;) {
        fprintf(stderr, "[ ] accepting on [%s]:%s ...\n", 
                srvr.addr, srvr.port);

        peer.fd = accept(srvr.fd, (struct sockaddr *)&sa, &sa_addrlen);
        if (peer.fd == -1) {
            fprintf(stderr, "[-] accept([%s]:%s): (%d) %s\n", 
                srvr.addr, srvr.port, errno, strerror(errno));
            if (errno == ENOTSOCK) {
                fprintf(stderr, "[-] programming error\n");
                goto error_cleanup;
            }
        } else
            break;
    }

    /* Pretty print the incoming address/port */
    err = getnameinfo((struct sockaddr *)&sa, sa_addrlen,
                    peer.addr, sizeof(peer.addr),
                    peer.port, sizeof(peer.port),
                    NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
        goto error_cleanup;
    }
    fprintf(stderr, "[+] accept([%s]:%s) from [%s]:%s fd=%d\n", 
            srvr.addr, srvr.port, peer.addr, peer.port, peer.fd);

    for (;;) {
        fd_set readset;
        fd_set writeset;
        fd_set errset;
        
        FD_ZERO(&readset);
        //FD_ZERO(&writeset);
        FD_ZERO(&errset);
        FD_SET(peer.fd, &readset);
        FD_SET(peer.fd, &writeset);
        FD_SET(peer.fd, &errset);

        struct timeval tv = {1,1};
        //fprintf(stderr, "[ ] select()ing ...\n");
        err = select(peer.fd + 1, &readset, &writeset, &errset, &tv);
        if (err < 0) {
            fprintf(stderr, "[-] select(): %s\n", strerror(errno));
            goto error_cleanup;
        } else if (err == 0) {
            fprintf(stderr, "[+] select() timeout\n");
        }

        if (FD_ISSET(peer.fd, &readset)) {
            char buf[512];
            int count;
            sleep(1);
            continue;
            
            count = recv(peer.fd, buf, sizeof(buf), 0);
            if (count == 0) {
                fprintf(stderr, "[+] recv() from [%s]:%s connection closed.\n", 
                    peer.addr, peer.port);
                close(peer.fd);
                peer.fd = -1;
                break;
            } else {
                fprintf(stderr, "[+] recv() %d-bytes from [%s]:%s\n", 
                    count, peer.addr, peer.port);
            }
        } else if (FD_ISSET(peer.fd, &errset)) {
            int errcode = 0;
            socklen_t sizeof_errcode = sizeof(errcode);
            err = getsockopt(peer.fd, SOL_SOCKET, SO_ERROR, &errcode, &sizeof_errcode);
            if (err) {
                fprintf(stderr, "[-] select(): %s\n", strerror(errno));
                goto error_cleanup;
            } else {
                fprintf(stderr, "[-] connect(): %s\n", strerror(errno));
                goto error_cleanup;
            }
        }
    }
error_cleanup:
    return 0;
}
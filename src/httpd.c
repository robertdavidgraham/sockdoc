/*
    This is a basic web server.
*/
#include "http-parse.h"
#include "smack.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h> /* fcntl(), F_GETFL, O_NONBLOCK */
#include <netinet/tcp.h>

/* Set with command-line '-d' otion to enable debug printf() */
int is_debug = 0;



struct url
{
    char *prefix;
    char *filename;
    int type;
    void *userdata;
    int (*callback_open)(struct connection *c);
    int (*callback_read)(struct connection *c);
    int (*callback_write)(struct connection *c);
    int (*callback_close)(struct connection *c, int status);
};

struct configuration
{
    const char *hostname;
    const char *portname;
    unsigned milliseconds_timeout;
    struct url *urls;
    size_t url_count;
};

struct connection
{
    char *buf; 
    size_t len;
    int fd;
    bool is_writing;
    struct url *url;
    char addrname[NI_MAXHOST];
    char portname[NI_MAXSERV];
};

struct httpserver
{
    struct connection connections[FD_SETSIZE];
    size_t connection_count;
};



enum {
    SOCKETS_NONBLOCKING     = 0x00000001,
    SOCKETS_BLOCKING        = 0x00000002,
    SOCKETS_IMEDIATE        = 0x00000004,
};

struct http_request
{
    
};

bool
my_isspace(int c)
{
    return isspace(c&0xFF);
}

int
parse_http_request(unsigned *state, unsigned *state2, struct httpserver *httpd, struct http_request *req, const unsigned char *buf, size_t length)
{
    unsigned s = *state;
    size_t i;
    size_t id;
    enum {
        S_LEADING_SPACE,
        S_METHOD,
    };

    for (i=0; i<length; i++) {
        unsigned char c = buf[i];
        switch (s) {
            case S_LEADING_SPACE:
                if (my_isspace(c))
                    continue;
                TRANSITION_NOW(S_METHOD);
                break;
            case S_METHOD:
                req->method = smack_search_next(
                        httpd->methods,
                        state2,
                        buf, &i, length);
                i--;
                if (req->method == SMACK_NOT_FOUND)
                    continue;
        }
    }

    *state = s;
    return 0;
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

int
wrap_listen(const char *addrname, const char *portname, int flags)
{
    struct addrinfo *ai = 0;
    struct addrinfo hints = {0};
    int err;
    int fd = -1;
    char hostaddr[NI_MAXHOST];
    char hostport[NI_MAXSERV];

    /* Convert address/port into a sockaddr structure */
    hints.ai_flags = AI_PASSIVE;
    err = getaddrinfo(addrname, portname, 0, &ai);
    if (err) {
        fprintf(stderr, "[-] getaddrinfo([%s]:%s): %s\n", 
                addrname, portname, gai_strerror(err));
        return -1;
    }

    /* And retrieve back again which addresses were assigned */
    err = getnameinfo(ai->ai_addr, ai->ai_addrlen,
                        hostaddr, sizeof(hostaddr),
                        hostport, sizeof(hostport),
                        NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
        goto error_cleanup;
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

    /* Allow multiple processes to share this IP address */
    err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (err) {
        fprintf(stderr, "[-] SO_REUSEADDR([%s]:%s): %s\n", hostaddr, hostport, strerror(errno));
        goto error_cleanup;
    }
    
#if defined(SO_REUSEPORT)
    /* Allow multiple processes to share this port */
    err = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
    if (err) {
        fprintf(stderr, "[-] SO_REUSEPORT([%s]:%s): %s\n", hostaddr, hostport, strerror(errno));
        goto error_cleanup;
    }
#endif

    /* Tell it to use the local port number (and optionally, address) */
    err = bind(fd, ai->ai_addr, ai->ai_addrlen);
    if (err) {
        fprintf(stderr, "[-] bind([%s]:%s): %s\n", hostaddr, hostport, strerror(errno));
        goto error_cleanup;
    }

    /* Configure the socket for listening (i.e. accepting incoming connections) */
    err = listen(fd, 10);
    if (err) {
        fprintf(stderr, "[-] listen([%s]:%s): %s\n", hostaddr, hostport, strerror(errno));
        goto error_cleanup;
    }

    free(ai);
    return fd;

error_cleanup:
    if (ai)
        freeaddrinfo(ai);
    if (fd != -1)
        close(fd);
    return -1;
}

/**
 * This wraps a call to `accept()`. It creates a new connection record in our
 * server, formats the strings for the address/port for later logging, and
 * sets flags on the connection
 */
int
wrap_accept(struct httpserver *httpd, int fd)
{
    int fd2 = -1;
    struct sockaddr_storage peer;
    socklen_t peer_addrlen = sizeof(peer);
    int err;
    struct connection *c = &httpd->connections[httpd->connection_count];

    
    /* Create a socket for the incoming connection */
    fd2 = accept(fd, &peer, &peer_addrlen);
    if (fd2 == -1) {
        fprintf(stderr, "[-] accept(): %s\n", strerror(errno));
        goto error_cleanup;
    }

    /* Make sure we have sufficient space to hold it */
    if (httpd->connection_count > sizeof(httpd->connections)/sizeof(httpd->connections[0])) {
        fprintf(stderr, "[-] out of connection space\n");
        goto error_cleanup;
    }

    /* Initialize this connection record */
    memset(c, 0, sizeof(*c));
    c->fd = fd2;

    /* Pretty print the incoming address/port */
    err = getnameinfo((struct sockaddr *)&peer, peer_addrlen,
                    c->addrname, sizeof(c->addrname),
                    c->portname, sizeof(c->portname),
                    NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
        goto error_cleanup;
    }
    fprintf(stderr, "[+] accept() from [%s]:%s\n", c->addrname, c->portname);

    httpd->connection_count=+;
    return 0;

error_cleanup:
    if (fd2 != -1)
        close(fd2);
    return -1;
}

int
wrap_receive(struct httpserver *httpd, size_t index)
{
    char buf[512];
    ssize_t count;
    struct connection *c = &httpd->connections[index];

    /* Receive a buffer */
    count = recv(c->fd, buf, sizeof(buf), 0);
    if (count == 0) {
        fprintf(stderr, "[+] close() from [%s]:%s\n", c->addrname, c->portname);
        c->url->callback_close(c, 0);
        return 0;
    } else if (count == -1) {
        fprintf(stderr, "[-] error from [%s]:%s: %s\n", c->addrname, c->portname, strerror(errno));
        c->url->callback_close(c, errno);
        return 0;
    } else {
        fprintf(stderr, "[+] recv([%s]:%s) %d bytes\n", c->addrname, c->portname, (int)count);
    }

    /* Parse the HTTP header */
}                

         

int main(int argc, char *argv[])
{
    int fd;
    struct configuration config = {0};
    struct httpserver httpd = {0};

    /* Ignore the send() problem */
    signal(SIGPIPE, SIG_IGN);

    /* Parse the configuration */
    config = parse_command_line(argc, argv);
    if (config.portname == NULL)
        config.portname = "80";
    if (config.milliseconds_timeout == 0)
        config.milliseconds_timeout = 100;

    /* Create a listening server socket */
    fd = wrap_listen(config.hostname, config.portname, 0);
    if (fd == -1) {
        fprintf(stderr, "[-] failed to create server, exiting...\n");
        return 1;
    }

    /* Sit in dispatch loop */
    for (;;) {
        fd_set readset;
        fd_set writeset;
        fd_set errset;
        int nfds;
        int err;
        struct timeval tv;
        size_t count;
        size_t i;

        tv.tv_sec = config.milliseconds_timeout / 1000;
        tv.tv_usec = config.milliseconds_timeout * 1000;

        /* Zero the sets */
        FD_ZERO(&readset);
        FD_ZERO(&writeset);
        FD_ZERO(&errset);

        /* Add the server socket */
        nfds = fd;
        FD_SET(fd, &readset);
        FD_SET(fd, &writeset);
        FD_SET(fd, &errset);
        for (i=0; i<httpd.connection_count; i++) {
            if (nfds < httpd.connections[i].fd)
                nfds = httpd.connections[i].fd;
            FD_SET(httpd.connections[i].fd, &readset);
            FD_SET(httpd.connections[i].fd, &writeset);
            FD_SET(httpd.connections[i].fd, &errset);
        }

        /*
         * Do the select, waiting for either incoming connections,
         * for incoming data on connections, or for errors.
         */
        err = select(nfds+1, &readset, &writeset, &errset, &tv);
        if (err < 0) {
            fprintf(stderr, "[-] select() error: %s\n", strerror(errno));
            break;
        }
        if (err == 0) {
            /* timeout reached with no activity, so loop around again */
            continue;
        }

        if (FD_ISSET(fd, &readset) || FD_ISSET(fd, &writeset) || FD_ISSET(fd, &errset)) {
            wrap_accept(&httpd, fd);
        }

        for (i=0; i<httpd.connection_count; i++) {
            if (FD_ISSET(httpd.connections[i].fd, &readset))
                wrap_receive(&httpd, i);
            if (FD_ISSET(httpd.connections[i].fd, &writeset))
                wrap_write(&httpd, i);
            if (FD_ISSET(httpd.connections[i].fd, &errset))
                wrap_error(&httpd, i);
        }


    }
    

        /* Loop on this connection receiving/transmitting data */
        for (;;) {
            char buf[512];
            ssize_t bytes_received;
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


}
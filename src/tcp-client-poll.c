/* tcp-client-poll
 Simple example of TCP client written with 'poll()'.
 This is an 'echo' client that opens many connections with servers.
 Example usage:
    tcp-client-poll -s 0.0.0.0 -t 10.0.0.129:7777 -t 10.0.0.130:7777 -c 100000
 Use the -s option one or more times to bind to local addresses.
 Use the -t at least one time to specify which target addresses
 to connect to.
 To get more than 65535 connections, more than one source or target needs to
 be specified.
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
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netdb.h>
#include <sys/resource.h>

struct my_connection
{
    size_t bytes_received;
    size_t bytes_sent;
    struct sockaddr_in6 sa;
    socklen_t sa_addrlen;
    ptrdiff_t len;
    char peeraddr[64];
    char peerport[8];
    char hostaddr[64];
    char hostport[8];
    char buf[512];
};

struct my_dispatcher
{
    struct my_connection *connections;
    struct pollfd *list;
    size_t count;
    size_t max;
    struct addrinfo **sources;
    size_t sources_count;
    size_t sources_index;
    struct addrinfo **targets;
    size_t targets_count;
    size_t targets_index;
};

struct my_dispatcher *dispatcher_create()
{
    struct my_dispatcher *dispatcher;

    dispatcher = malloc(sizeof(*dispatcher));
    memset(dispatcher, 0, sizeof(*dispatcher));

    return dispatcher;
}

void dispatcher_alloc_connections(struct my_dispatcher *dispatcher, long n)
{
    size_t size;
    dispatcher->max = n;
    
    size = dispatcher->max * sizeof(*dispatcher->list);
    dispatcher->list = malloc(size);
    memset(dispatcher->list, 0, size);
    
    size = dispatcher->max * sizeof(*dispatcher->connections);
    dispatcher->connections = malloc(size);
    memset(dispatcher->connections, 0, size);
}

int is_decimal(const char *str)
{
    size_t i;
    for (i=0; str[i]; i++) {
        if (!isdigit(str[i]))
            return 0;
    }
    return 1;
}

int index_of(const char *str, char c)
{
    int i;
    for (i=0; str[i]; i++) {
        if (str[i] == c)
            return i;
    }
    return -1;
}

int rindex_of(const char *str, char c)
{
    int result = -1;
    int i;
    for (i=0; str[i]; i++) {
        if (str[i] == c)
            result = i;
    }
    return result;
}

/**
 * Split a URL-type address into 'hostname' and 'port' parts
 * localhost:80 -> "localhost" and "80"
 * [::1]:1024 -> "::1" and "1024"
 */
void split_address(const char *name, char **r_addr, char **r_port)
{
    char *addr = NULL;
    size_t addrlen;
    char *port = NULL;
    size_t portlen;
    size_t namelen = strlen(name);

    if (is_decimal(name)) {
        addr = NULL;
        port = strdup(name);
    } else if (index_of(name, ':') && index_of(name, ':') == rindex_of(name, ':')) {
        addrlen = index_of(name, ':');;
        portlen = namelen - addrlen - 1;
        addr = malloc(addrlen + 1);
        memcpy(addr, name, addrlen + 1);
        addr[addrlen] = '\0';
        port = malloc(portlen + 1);
        memcpy(port, name + addrlen + 1, portlen + 1);
        port[portlen] = '\0';
    } else if (name[0] == '[' && strchr(name+1, ']')) {
        addrlen = index_of(name, ']') - 1;
        addr = malloc(addrlen + 1);
        memcpy(addr, name + 1, addrlen + 1);
        addr[addrlen] = '\0';
        if (rindex_of(name, ':') > addrlen) {
            portlen = namelen - rindex_of(name, ':') - 1;
            port = malloc(portlen + 1);
            memcpy(port, name + rindex_of(name, ':') + 1, portlen + 1);
            port[portlen] = '\0';
        } else
            port = NULL;
    } else {
        addr = strdup(name);
        port = NULL;
    }

    *r_addr = addr;
    *r_port = port;
}

void dispatcher_add_source(struct my_dispatcher *dispatcher, const char *name)
{
    int err;
    char *addr;
    char *port;
    struct addrinfo *ai;
    struct addrinfo *addresses = 0;

    /* split address, in case port is also specified */
    split_address(name, &addr, &port);

    /* lookup name/port */
    err = getaddrinfo(addr,                 /* IPv4/IPv6/DNS address*/
                      port,                 /* port number */
                      0,                    /* hints */
                      &addresses);                 /* result */
    if (err) {
        fprintf(stderr, "[-] getaddrinfo(): %s\n", gai_strerror(err));
        return;
    }

    if (addr)
        free(addr);
    if (port)
        free(port);
    
    for (ai = addresses; ai; ai = ai->ai_next) {
        if (dispatcher->sources_count == 0)
            dispatcher->sources = malloc(sizeof(void*));
        else
            dispatcher->sources = realloc(dispatcher->sources, (dispatcher->sources_count+1) * sizeof(void*));
        dispatcher->sources[dispatcher->sources_count] = ai;
        dispatcher->sources_count++;
    }
}

void dispatcher_add_target(struct my_dispatcher *dispatcher, const char *name)
{
    int err;
    char *addr;
    char *port;
    struct addrinfo *ai;
    struct addrinfo *addresses = 0;

    /* split address, in case port is also specified */
    split_address(name, &addr, &port);

    /* lookup name/port */
    err = getaddrinfo(addr,                 /* IPv4/IPv6/DNS address*/
                      port,                 /* port number */
                      0,                    /* hints */
                      &addresses);                 /* result */
    if (err) {
        fprintf(stderr, "[-] getaddrinfo(): %s\n", gai_strerror(err));
        return;
    }
    
    for (ai = addresses; ai; ai = ai->ai_next) {
        if (dispatcher->targets_count == 0)
            dispatcher->targets = malloc(sizeof(void*));
        else
            dispatcher->targets = realloc(dispatcher->targets, (dispatcher->targets_count+1) * sizeof(void*));
        dispatcher->targets[dispatcher->targets_count] = ai;
        dispatcher->targets_count++;
    }
}

void dispatcher_add(struct my_dispatcher *dispatcher, int fd, struct sockaddr_in6 *sa, socklen_t sa_addrlen)
{
    struct my_connection *c;
    int err;
    
    /* add to the poll() list, set for reading */
    dispatcher->list[dispatcher->count].fd = fd;
    dispatcher->list[dispatcher->count].events = POLLIN;
    dispatcher->list[dispatcher->count].revents = 0;
    
    /* add per=connection info */
    c = &dispatcher->connections[dispatcher->count];
    c->len = 0; /* init buffer */
    c->sa_addrlen = sa_addrlen;
    memcpy(&c->sa, sa, sa_addrlen);
    c->bytes_sent = 0;
    c->bytes_received = 0;

    /* get print name of remote connection */
    err = getnameinfo(  (struct sockaddr *)&c->sa, c->sa_addrlen,
                        c->peeraddr, sizeof(c->peeraddr),
                        c->peerport, sizeof(c->peerport),
                        NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
        memcpy(c->peeraddr, "err", 4);
        memcpy(c->peerport, "err", 4);
    } else {
        ; //fprintf(stderr, "[+] connect() to [%s]:%s\n", c->peeraddr, c->peerport);
    }

    /* grow the lists by 1, if needed*/
    dispatcher->count++;
    if (dispatcher->count >= dispatcher->max) {
        dispatcher->max++;
        dispatcher->list = realloc(dispatcher->list, dispatcher->max * sizeof(*dispatcher->list));
        dispatcher->connections = realloc(dispatcher->connections, dispatcher->max * sizeof(*dispatcher->connections));
    }
}

void dispatcher_remove_at(struct my_dispatcher *dispatcher, size_t i)
{
    size_t end;

    /* close the socket if it's still open */
    if (dispatcher->list[i].fd > 0) {
        close(dispatcher->list[i].fd);
        dispatcher->list[i].fd = -1;
    }

    /* For efficiency, replace this entry with the one at the end of the list */
    end = dispatcher->count - 1;
    if (end > i) {
        memcpy(&dispatcher->list[i], &dispatcher->list[end], sizeof(dispatcher->list[0]));
        memcpy(&dispatcher->connections[i], &dispatcher->connections[end], sizeof(dispatcher->connections[0]));
    }
    dispatcher->count--;
}

void dispatcher_destroy(struct my_dispatcher *dispatcher)
{
    while (dispatcher->count)
        dispatcher_remove_at(dispatcher, dispatcher->count-1);

    free(dispatcher->list);
    free(dispatcher->connections);
}

void dispatcher_parse_command_line(struct my_dispatcher *dispatcher, int argc, char *argv[])
{
    int i;

    for (i=1; i<argc; i++) {
        const char *value;
        long n;

        if (argv[i][0] != '-') {
            value = argv[i];
            dispatcher_add_target(dispatcher, value);
        } else
        switch (argv[i][1]) {
            case 'c': /* count */
                if (argv[i][2] == '\0' && (i+1) < argc)
                    value = argv[++i];
                else
                    value = &argv[i][2];
                n = strtol(value, 0, 0);
                if (n < 1 || 1000000000 < n) {
                    fprintf(stderr, "[-] invalid connection count\n");
                    exit(1);
                } else {
                    dispatcher_alloc_connections(dispatcher, n);
                }
                break;
            case 's': /* source */
                if (argv[i][2] == '\0' && (i+1) < argc)
                    value = argv[++i];
                else
                    value = &argv[i][2];
                dispatcher_add_source(dispatcher, value);
                break;
            case 't': /* target */
                if (argv[i][2] == '\0' && (i+1) < argc)
                    value = argv[++i];
                else
                    value = &argv[i][2];
                dispatcher_add_target(dispatcher, value);
                break;
            default:
                fprintf(stderr, "[-] -%c: unknown option\n", isprint(argv[i][1])?argv[i][1]:'.');
                exit(1);
        }
    }
}

void dispatcher_getsockname(struct my_dispatcher *dispatcher, size_t i)
{
    int fd = dispatcher->list[i].fd;
    int err;
    struct sockaddr_in6 sin6;
    socklen_t sizeof_sin6 = sizeof(sin6);
    struct my_connection *c = &dispatcher->connections[i];
    err = getsockname(fd, (struct sockaddr*)&sin6, &sizeof_sin6);
    if (err) {
        fprintf(stderr, "[-] getsockname(): %s\n", strerror(errno));
        exit(1);
    }
    err = getnameinfo((struct sockaddr *)&sin6, sizeof_sin6,
                        c->hostaddr, sizeof(c->hostaddr),
                        c->hostport, sizeof(c->hostport),
                        NI_NUMERICHOST | NI_NUMERICSERV);

}

void dispatcher_connect_next(struct my_dispatcher *dispatcher)
{
    int fd;
    int err;
    struct addrinfo *ai;
    size_t i;

    ai = dispatcher->targets[dispatcher->targets_index];
    if (++dispatcher->targets_index >= dispatcher->targets_count)
        dispatcher->targets_index = 0;

    /* Create a socket */
    fd = socket(ai->ai_family, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "[-] socket(): %d: %s\n", errno, strerror(errno));
        switch (errno) {
            case EMFILE:
                fprintf(stderr, "[-] files=%d, use 'ulimit -n %d' to raise\n", (int)dispatcher->count, (int)dispatcher->max);;
                break;
        }
        exit(1);
    }

#if defined(FIONBIO)
    {
        int yes = 1;
        err = ioctl(fd, FIONBIO, (char *)&yes);
        if (err) {
            fprintf(stderr, "[-] ioctl(FIONBIO): %s\n", strerror(errno));
        }
    }
#elif defined(O_NONBLOCK) && defined(F_SETFL)
    {
        int flag;
        flag = fcntl(fd, F_GETFL, 0);
        flag |= O_NONBLOCK;
        fcntl(socketfd, F_SETFL,  flag);
    }
#else
    fprintf(stderr, "[-] non-blocking not set\n");
#endif
    
    /* Add to our poll list */
    dispatcher_add(dispatcher, fd, (struct sockaddr_in6*)ai->ai_addr, ai->ai_addrlen);
    i = dispatcher->count - 1;
    memcpy(dispatcher->connections[i].buf, "0123456789abcdef", 16);
    dispatcher->connections[i].len = 16;

    /* Try to connect */
    err = connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (err && (errno == EWOULDBLOCK || errno == EINPROGRESS)) {
        dispatcher_getsockname(dispatcher, i);
        //fprintf(stderr, "."); fflush(stderr);
        /* normal condition, except when on the same machine  */
        dispatcher->list[i].events = POLLOUT;
    } else if (err == 0) {
        /* normal when on same machine */
        dispatcher->list[i].events = POLLOUT;
    } else {
        fprintf(stderr, "[-] connect([%s]:%s): %d: %s\n",
            dispatcher->connections[i].peeraddr,
            dispatcher->connections[i].peerport,
            errno,
            strerror(errno));
        dispatcher_remove_at(dispatcher, i);
        exit(1);
        return;
    }
}


int main(int argc, char *argv[])
{
    int err;
    struct my_dispatcher *dispatcher = NULL;
    size_t i;
    
    /* Ignore the send() problem */
    signal(SIGPIPE, SIG_IGN);

    /* create an instance of our polling object */
    dispatcher = dispatcher_create();
    dispatcher_parse_command_line(dispatcher, argc, argv);
    if (dispatcher->targets_count == 0) {
        fprintf(stderr, "[-] no targets specified, use -t <target>\n");
        exit(1);
    } else
        fprintf(stderr, "[+] %d targets\n", (int)dispatcher->targets_count);
    if (dispatcher->max == 0) {
        dispatcher_alloc_connections(dispatcher, 100);
    }

    /* Create the next target */
    for (i=0; i<10 && i < dispatcher->targets_count; i++)
        dispatcher_connect_next(dispatcher);
    
    /* dispatch loop */
    while (dispatcher->count) {
        int timeout = 100; /* 100 milliseconds */
        size_t i;

        if (dispatcher->count < dispatcher->max)
            dispatcher_connect_next(dispatcher);

        /* wait for incoming event on any connection */
        err = poll(dispatcher->list, dispatcher->count, timeout);
        if (err == -1) {
            fprintf(stderr, "[-] poll(): %s\n", strerror(errno));
            switch (errno) {
                case EINVAL:
                    fprintf(stderr, "max file descriptor reached? nfds=%d\n", dispatcher->count);
                    {
                        struct rlimit rl;
                        getrlimit(RLIMIT_NOFILE, &rl);
                        fprintf(stderr, "rlimit cur=%lu max=%lu\n", rl.rlim_cur, rl.rlim_max);

                    }
                    break;
            }
            break;
        } else if (err == 0) {
            /* timeout happened, nothing was recevied */
            continue;
        }

        /* handle all the TCP connections */
        for (i=1; i<dispatcher->count; i++) {
            struct my_connection *c = &dispatcher->connections[i];
            if (dispatcher->list[i].revents == 0) {
                /* no events for this socket */
                continue;
            } else if ((dispatcher->list[i].revents & POLLHUP) != 0) {
                /* other side hungup (i.e. sent FIN, closed socket) */
                if (c->bytes_received == 0 && c->bytes_sent == 0) {
                    fprintf(stderr, "-"); fflush(stderr);
                    //fprintf(stderr, "[-] connect([%s]:%s): connection refused\n", c->peeraddr, c->peerport);
                } else {
                    fprintf(stderr, "sent=%u recv=%u\n", (unsigned)c->bytes_sent, (unsigned)c->bytes_received);
                    fprintf(stderr, "[+] close([%s}:%s -> [%s]:%s): connection closed gracefully\n", 
                        c->hostaddr, c->hostport,
                        c->peeraddr, c->peerport);
                    dispatcher_remove_at(dispatcher, i--);
                    exit(1);
                }
            } else if ((dispatcher->list[i].revents & POLLERR) != 0) {
                /* error, probably RST sent by other side, but to be sure,
                 * get the error associated with the socket */
                int opt;
                socklen_t opt_len = sizeof(opt);
                err = getsockopt(dispatcher->list[i].fd, SOL_SOCKET, SO_ERROR, &opt, &opt_len);
                if (err) {
                    /* should never happen*/
                    fprintf(stderr, "[-] getsockopt([%s]:%s): %s\n", c->peeraddr, c->peerport, strerror(errno));
                } else {
                    fprintf(stderr, "[-] recv([%s]:%s): %s\n", c->peeraddr, c->peerport, strerror(opt));
                }
                dispatcher_remove_at(dispatcher, i--);
                exit(1);
            } else if ((dispatcher->list[i].revents & POLLIN) != 0) {
                /* Data is ready to receive */
                c->len = recv(dispatcher->list[i].fd, c->buf, sizeof(c->buf), 0);
                if (c->len == 0 ) {
                    /* Shouldn't be possible, should've got POLLHUP instead */
                    fprintf(stderr, "[-] RECV([%s]:%s): %s\n", c->peeraddr, c->peerport, "CONNECTION CLOSED");
                    dispatcher_remove_at(dispatcher, i--);
                    exit(1);
                } else if (c->len < 0) {
                    fprintf(stderr, "[-] RECV([%s]:%s): %s\n", c->peeraddr, c->peerport, strerror(errno));
                    dispatcher_remove_at(dispatcher, i--);
                    exit(1);
                } else {
                    /* change poll() entry to transmit instead of receive */
                    //fprintf(stderr, "[+] recv([%s]:%s): received %d bytes\n", c->peeraddr, c->peerport, (int)c->len);
                    c->bytes_received += c->len;
                    dispatcher->list[i].events = POLLOUT;
                }
            } else if ((dispatcher->list[i].revents & POLLOUT) != 0) {
                /* We are ready to transmit data */
                ptrdiff_t bytes_sent;
                if (c->bytes_received == 0 && c->bytes_sent == 0) {
                    fprintf(stderr, "+"); fflush(stderr);
                }
                
                bytes_sent = send(dispatcher->list[i].fd, c->buf, c->len, 0);
                if (bytes_sent < 0) {
                    /* might've reset connection between poll() and send() */
                    fprintf(stderr, "[-] SEND([%s]:%s -> [%s]:%s): %s\n", 
                        c->hostaddr, c->hostport,
                        c->peeraddr, c->peerport, strerror(errno));
                    dispatcher_remove_at(dispatcher, i);
                    exit(1);
                } else if (bytes_sent < c->len) {
                    /* hit the send() incomplete issue */
                    fprintf(stderr, "[+] SEND([%s]:%s): %s\n", c->peeraddr, c->peerport, "out of buffer");
                    memmove(c->buf, c->buf+bytes_sent, c->len - bytes_sent);
                    c->len -= bytes_sent;
                    c->bytes_sent += bytes_sent;
                    dispatcher->list[i].events = POLLOUT;
                } else {
                    /* all the bytes have been sent, so go back to reading */
                    //fprintf(stderr, "[+] send([%s]:%s): sent %d bytes\n", c->peeraddr, c->peerport, (int)bytes_sent);
                    c->bytes_sent += bytes_sent;
                    dispatcher->list[i].events = POLLIN;
                }
            } else {
                fprintf(stderr, "[-] poll([%s]:%s): unknown event[%d] 0x%x\n", c->peeraddr, c->peerport, (int)i, dispatcher->list[i].revents);
                dispatcher_remove_at(dispatcher, i--);
                exit(1);
            }
        } /* end handling connections */
    } /* end dispatch loop */

    

    if (dispatcher)
        dispatcher_destroy(dispatcher);
    return 0;
}


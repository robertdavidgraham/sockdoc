#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

/**
 * This function creates a listening socket that we can try to connect to
 * in order to generate some errors
 */
int
create_listener(void)
{
    int err;
    int port = -1;


    /* 
     * Get an address structure for the port 
     * NULL = any IP address
     * "0" = port number 0 means select any available port 
     */
    struct addrinfo *ai = NULL;
    struct addrinfo hints = {0};
    hints.ai_flags = AI_PASSIVE;
    err = getaddrinfo(0,                /* local address, NULL=any */
                      "0",              /* local port number, "0"=any */
                      &hints,           /* hints */
                      &ai);             /* result */
    if (err) {
        fprintf(stderr, "[-] getaddrinfo(): %s\n", gai_strerror(err));
        goto cleanup;
    }

    /* 
     * Create a file handle for the kernel resources 
     */
    int fd;
    fd = socket(ai->ai_family, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "[-] socket(): %s\n", strerror(errno));
        goto cleanup;
    }

    /* Allow multiple processes to share this IP address,
     * to avoid any errors that might be associated with
     * timeouts and such. */
    int yes = 1;
    err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (err) {
        fprintf(stderr, "[-] SO_REUSEADDR([): %s\n", strerror(errno));
        goto cleanup;
    }
    
#if defined(SO_REUSEPORT)
    /* Allow multiple processes to share this port */
    err = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
    if (err) {
        fprintf(stderr, "[-] SO_REUSEPORT(): %s\n", strerror(errno));
        goto cleanup;
    }
#endif

    /* Tell it to use the local port number (and optionally, address) */
    err = bind(fd, ai->ai_addr, ai->ai_addrlen);
    if (err) {
        fprintf(stderr, "[-] bind(): %s\n", strerror(errno));
        goto cleanup;
    }

    /* Get the name of the port/IP that was chosen for the this socket */
    struct sockaddr_storage sa = {0};
    socklen_t sa_len = sizeof(sa);
    err = getsockname(fd, (struct sockaddr *)&sa, &sa_len);
    if (err) {
        fprintf(stderr, "[-] getsockname(): %s\n", strerror(errno));
        goto cleanup;
    }

    /* Format the addr/port for pretty printing */
    char hostaddr[NI_MAXHOST];
    char hostport[NI_MAXSERV];
    err = getnameinfo((struct sockaddr*)&sa, sa_len,
                        hostaddr, sizeof(hostaddr),
                        hostport, sizeof(hostport),
                        NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
        goto cleanup;
    }
    port = atoi(hostport);
    
    /* Configure the socket for listening (i.e. accepting incoming connections) */
    err = listen(fd, 10);
    if (err) {
        fprintf(stderr, "[-] listen([%s]:%s): %s\n", hostaddr, hostport, strerror(errno));
        goto cleanup;
    } else
        fprintf(stderr, "[+] listening on [%s]:%s\n", hostaddr, hostport);



cleanup:
    if (ai)
        freeaddrinfo(ai);
    return port;
}

void
error_duplicate_connection(int port)
{
    char portname[64];
    snprintf(portname, sizeof(portname), "%d", port);


}

/**
 * Tests whether a pointer is bad. It does this by calling
 * a system call with the pointer and testing to see whether
 * EFAULT is returned. In this cas,e the write() function is
 * chosen, but a bunch of other system calls can be used.
 */
bool
is_valid_pointer(const void *p, size_t len)
{
    int fd;
    int err;

    if (len == 0)
        len = 1;

    /* open a standard file that we can write to */
    fd  = open("/dev/random", O_WRONLY);
    if (fd == -1) {
        fprintf(stderr, "[-] %s: open(/dev/random): %s\n", __func__, strerror(errno));
        return false;
    }

    /* Try writing */
    err = write(fd, p, len);
    if (err < 0 && errno == EFAULT) {
        close(fd);
        return false;
    } else if (err < 0) {
        fprintf(stderr, "[-] %s: write(/dev/random): %s\n", __func__, strerror(errno));
        close(fd);
        return false;
    }

    close(fd);
    return true;
}

int
main(int argc, char **argv)
{
    int port;

    /* Ignore the send() problem */
    signal(SIGPIPE, SIG_IGN);

    /* Use EFAULT to test pointers */
    if (!is_valid_pointer("", 1))
        printf("[-] empty string is invalid\n");
    if (is_valid_pointer((void*)1, 1))
        printf("[-] 1 is a valid pointer\n");
    if (is_valid_pointer(0, 1))
        printf("[-] 0 is a valid pointer\n");
    
    port = create_listener();


    error_duplicate_connection(port);

    return 0;
}
/* tcp-send-fail
 Demos 'send()' failing to send all its data under load.
 Also used to trigger 'SIGPIPE' errors in servers by early 'close()'.
 Example usage:
    tcp-send-fail www.google.com 80
    tcp-send-fail 127.0.0.1 7777
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
#include <netdb.h>


int main(int argc, char *argv[])
{
    struct addrinfo *addresses = NULL;
    struct addrinfo *ai;
    int err;
    int fd = -1;
    char *buf;
    size_t buf_length;
    size_t i;
    size_t total_sent = 0;
    
    /* build a transmit buffer with a pattern we can recognize on the wire */
    buf_length = 80 * 100;
    buf = malloc(buf_length);
    for (i = 0; i<buf_length; ) {
        size_t j;
        for (j=0; j<26; j++)
            buf[i++] = 'a' + j;
        for (j=0; j<10; j++)
            buf[i++] = '0' + j;
        for (j=0; j<26; j++)
            buf[i++] = 'A' + j;
        for (j=0; j<10; j++)
            buf[i++] = '0' + j;
        for (j=0; j<6; j++)
            buf[i++] = "-=(){}"[j];
        buf[i++] = '\r';
        buf[i++] = '\n';
    }

    if (argc != 3) {
        fprintf(stderr, "[-] usage: tcp-client-fail <host> <port>\n");
        return -1;
    }
    
    /* Do a DNS lookup on the name */
    err = getaddrinfo(argv[1],      /* host name */
                      argv[2],      /* port number */
                      0,            /* hints (defaults) */
                      &addresses);  /* results */
    if (err) {
        fprintf(stderr, "[-] getaddrinfo(): %s\n", gai_strerror(err));
        return -1;
    } else {
	    int count = 0;
	    for (ai=addresses; ai; ai = ai->ai_next)
	        count++;
	    fprintf(stderr, "[+] getaddrinfo(): returned %d addresses\n", count);
    }
    
    /* Of the several results, keep trying to connect until
     * we get one that works */
    for (ai=addresses; ai; ai = ai->ai_next) {
        char addrname[NI_MAXHOST];
        char portname[NI_MAXSERV];
        
        /* Print the address/port to strings for logging/debugging  */
        err = getnameinfo(ai->ai_addr, ai->ai_addrlen,
                          addrname, sizeof(addrname),
                          portname, sizeof(portname),
                          NI_NUMERICHOST | NI_NUMERICSERV);
        if (err) {
            fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
            goto cleanup;
        }
        
        /* Create a socket */
        fd = socket(ai->ai_family, SOCK_STREAM, 0);
        if (fd == -1) {
            fprintf(stderr, "[-] socket(): %s\n", strerror(errno));
            goto cleanup;
        }
        
        /* Try to connect */
        err = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (err) {
            fprintf(stderr, "[-] connect([%s]:%s): %s\n", addrname, portname, strerror(errno));
            close(fd);
            fd = -1;
            continue;
        } else {
            fprintf(stderr, "[+] connect([%s]:%s): %s\n", addrname, portname, "succeeded");
            break; /* got one that works, so break out of loop */
        }
    }
    if (fd == -1) {
        fprintf(stderr, "error: no successful connection\n");
        //goto cleanup;
    }

    /* Find the send-buffer size */
#if defined(SO_SNDBUF)
    {
        int count;
        socklen_t len = sizeof(count);
        err = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &count, &len);
        if (err) {
            fprintf(stderr, "[-] getsockopt(SO_SNDBUF): %s\n", strerror(errno));
        } else {
            fprintf(stderr, "[+] send buffer size = %d\n", count);
        }

    }
#endif

    /* Set the socket to non-blocking */
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
        if (flag == -1) {
            fprintf(stderr, "[-] fcntl(F_GETFL): %s\n", sterror(errno));
        }
        flag |= O_NONBLOCK;
        err = fcntl(socketfd, F_SETFL,  flag);
        if (flag == -1) {
            fprintf(stderr, "[-] fcntl(O_NONBLOCK): %s\n", sterror(errno));
        }
    }
#else
    fprintf(stderr, "[-] non-blocking not set\n");
#endif

    /* Send a lot of data, to overload our internal buffers */    
    for (i=0; i<65536; i++) {
        ptrdiff_t count;

        count = send(fd, buf, buf_length, 0);
        if (count == -1) {
            fprintf(stderr, "[-] send(): %s\n", strerror(errno));
            break;
        } else if (count < buf_length) {
            fprintf(stderr, "[+] send() sent %d bytes out of %d\n", 
                    (int)(total_sent + count), (int)(total_sent + buf_length));
            break;
        } else
            total_sent += count;

    }

    fprintf(stderr, "[+] done.\n");


cleanup:
    if (fd > 0)
        close(fd);
    if (addresses)
        freeaddrinfo(addresses);
}


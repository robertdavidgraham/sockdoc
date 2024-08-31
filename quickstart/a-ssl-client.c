#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include <netdb.h> /* getaddrinfo() */
#include <sys/socket.h> /* socket(), connect(), send(), recv() */
#include <unistd.h> /* close() */

#include <openssl/ssl.h>
#include <openssl/err.h>


/* Mimics an HTTP request */
static const char *my_http_request = 
    "HEAD / HTTP/1.0\r\n"
    "User-Agent: tcp_client/0.0\r\n"
    "\r\n";

/* Prints to command-line the message we receive in response */
void
print_string(const unsigned char *buf, ssize_t count) {
    ssize_t i;
    if (count <= 0)
        return;
    for (i = 0; i < count; i++) {
            unsigned char c = buf[i];
            if (isprint(c) || isspace(c))
                printf("%c", c);
            else
                printf("."); /* non-printable characters */
        }
}

int main(int argc, char *argv[]) {
    struct addrinfo *target = NULL;
    int err;
    int fd;
    ssize_t count;
    unsigned char buf[65536];
    const char *hostname = argv[1];
    const char *portname = argv[2];
    SSL_CTX *ctx;
    SSL *ssl;

    if (argc != 3) {
        printf("usage: %s <hostname> <portnum>\n", argv[0]);
        exit(1);
    }


    SSL_library_init();
    OpenSSL_add_all_algorithms(); 
    ctx = SSL_CTX_new(TLS_client_method());
    
    err = getaddrinfo(hostname, portname, 0, &target);
    fd = socket(target->ai_family, SOCK_STREAM, 0);
    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    err = connect(fd, target->ai_addr, target->ai_addrlen);
    err = SSL_connect(ssl);
    SSL_write(ssl, my_http_request, strlen(my_http_request));
    count = SSL_read(ssl, &buf, sizeof(buf)); 
    print_string(buf, count);     
    SSL_free(ssl); 
    close(fd);
    SSL_CTX_free(ctx);

    return 0;
}

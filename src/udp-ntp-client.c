/* udp-ntp-client
   Sends NTP requests to the list of NTP targets.
 */
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <stdlib.h>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#define NTP_TIMESTAMP_DELTA 2208988800ull

unsigned char ntp_req[48] = {0x1B, 0};


typedef struct {
    uint8_t li_vn_mode;
    uint8_t stratum;
    uint8_t poll;
    uint8_t precision;
    uint32_t root_delay;
    uint32_t root_dispersion;
    uint32_t ref_id;
    uint32_t ref_tm_s;
    uint32_t ref_tm_f;
    uint32_t orig_tm_s;
    uint32_t orig_tm_f;
    uint32_t rx_tm_s;
    uint32_t rx_tm_f;
    uint32_t tx_tm_s;
    uint32_t tx_tm_f;
} ntp_packet;

static unsigned char
READ8(const unsigned char *buf, size_t *offset, size_t max) {
    if (*offset + 1 < max)
        return buf[(*offset)++];
    else
        return ~0;
}
static unsigned
READ32(const unsigned char *buf, size_t *offset, size_t max) {
    if (*offset + 4 < max) {
        size_t i = *offset;
        (*offset) += 4;
        return buf[i+0]<<24 | buf[i+1]<<16 | buf[i+2]<<8 | buf[i+3];
    } else
        return ~0;
}

static ntp_packet
parse_ntp(const unsigned char *buf, size_t max) {
    ntp_packet ntp;
    size_t offset = 0;

    ntp.li_vn_mode = READ8(buf, &offset, max);
    ntp.stratum = READ8(buf, &offset, max);
    ntp.poll = READ8(buf, &offset, max);
    ntp.precision = READ8(buf, &offset, max);
    ntp.root_delay = READ32(buf, &offset, max);
    ntp.root_dispersion = READ32(buf, &offset, max);
    ntp.ref_id = READ32(buf, &offset, max);
    ntp.ref_tm_s = READ32(buf, &offset, max);
    ntp.ref_tm_f = READ32(buf, &offset, max);
    ntp.orig_tm_s = READ32(buf, &offset, max);
    ntp.orig_tm_f = READ32(buf, &offset, max);
    ntp.rx_tm_s = READ32(buf, &offset, max);
    ntp.rx_tm_f = READ32(buf, &offset, max);
    ntp.tx_tm_s = READ32(buf, &offset, max);
    ntp.tx_tm_f = READ32(buf, &offset, max);

    return ntp;
}

static int
LOG_sending_from(int fd) {
    int err;
    char addrstring[64];
    char portstring[8];
    struct sockaddr_storage localaddr;
    socklen_t localaddr_length = sizeof(localaddr);

    /* Get the LOCAL address bound to this socket descriptor */
    err = getsockname(fd, (struct sockaddr*)&localaddr, &localaddr_length);
    if (err) {
        fprintf(stderr, "[-] getsockname(): %s\n", strerror(errno));
        return err;
    }

    /* Format that address as human readable strings */
    err = getnameinfo((struct sockaddr*)&localaddr, localaddr_length,
                        addrstring, sizeof(addrstring),
                        portstring, sizeof(portstring),
                        NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
        return -1;
    }

    /* Print the strings */
    fprintf(stderr, "[+] sending FROM [%s]:%s\n",
            addrstring, portstring);

    return 0;
}

static struct sockaddr_storage
unwrap_addr(const struct sockaddr *addr, socklen_t addrlen) {
    struct sockaddr_storage result = {0};

    /* If the addrlen is not known by the caller, then figure this out
     * for ourselves based upon family. */
    if (addrlen == 0) {
        switch (addr->sa_family) {
            case AF_INET: addrlen = sizeof(struct sockaddr_in); break;
            case AF_INET6: addrlen = sizeof(struct sockaddr_in6); break;
            default:
                fprintf(stderr, "[-] unwrap() unknown family, aborting...\n");
                abort();
        }
    }
    memcpy(&result, addr, addrlen);

    /* If this is not IPv6, then just return whatever it was originally */
    if (addr->sa_family != AF_INET6) {
        return result;
    }

    /* If this IPv6 address isn't wrapping an IPv4 address, return
     * the original address */
    const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *)addr;
    if (!IN6_IS_ADDR_V4MAPPED(&addr6->sin6_addr)) {
        return result;
    }

    /* Extract the lower 32-bit bits from the IPv4 address field */
    struct sockaddr_in *addr4 = (struct sockaddr_in *)&result;
    addr4->sin_family = AF_INET;
    addr4->sin_port = addr6->sin6_port;
    memcpy(&addr4->sin_addr.s_addr, &addr6->sin6_addr.s6_addr[12], 4);

    return result;
}

static int
LOG_sending_to(const struct sockaddr *addr, socklen_t addrlen) {
    char addrstring[64];
    char portstring[8];
    int err;
    struct sockaddr_storage ss = unwrap_addr(addr, addrlen);

    err = getnameinfo(  (struct sockaddr*)&ss, sizeof(ss), 
                        addrstring, sizeof(addrstring), 
                        portstring, sizeof(portstring),
                        NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
        goto cleanup;
    }
    fprintf(stderr, "[+] sending TO [%s]:%s\n", addrstring, portstring);
    return 0;
cleanup:
    return 1;
}

static int
LOG_receiving_from(const struct sockaddr *addr, socklen_t addrlen, const char *msg) {
    char addrstring[64];
    char portstring[8];
    int err;
    struct sockaddr_storage ss = unwrap_addr(addr, addrlen);

    err = getnameinfo(  (struct sockaddr*)&ss, sizeof(ss), 
                        addrstring, sizeof(addrstring), 
                        portstring, sizeof(portstring),
                        NI_NUMERICHOST | NI_NUMERICSERV);
    if (err) {
        fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
        goto cleanup;
    }

    fprintf(stderr, "[+] [%s]:%s: %s\n", addrstring, portstring, msg);
    return 0;
cleanup:
    return 1;
}


/*
 * Create a UDP socket for later use with sendto()/recvfrom().
 * This will be both IPv6 and IPv4.
 */
int
create_udp_socket(const char *source_addr, const char *source_port) {
    int fd;
    int err;


    /* We create an IPv6 socket first, even though we'll use it for IPv4 too. */
    fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd == -1) {
        fprintf(stderr, "[-] socket(): %s\n", strerror(errno));
        return -1;
    }

    /* Configure both IPv4 and IPv6 usage on the socket */
    int off = 0;
    err = setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
    if (err) {
        fprintf(stderr, "[-] setsockopt(IPV6_V6ONLY): %s\n", strerror(errno));
        goto fail;
    }

    struct addrinfo hints = {0};
    hints.ai_family = AF_INET6;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_DGRAM;
    if (source_port == NULL)
        source_port = "0";

    struct addrinfo *ai = NULL;
    err = getaddrinfo(source_addr, source_port, &hints, &ai);
    if (err) {
        fprintf(stderr, "[-] getaddrinfo(): %s\n", gai_strerror(err));
        goto fail;
    }
    err = bind(fd, ai->ai_addr, ai->ai_addrlen);
    if (err) {
        fprintf(stderr, "[-] bind([%s]:%s): %s\n",
            source_addr, source_port, strerror(errno));
        goto fail;
    }
    freeaddrinfo(ai);

    LOG_sending_from(fd);
    return fd;
fail:
    if (fd != -1)
        close(fd);
    return -1;
}

static void
process_response(int fd) {
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    char buf[65536];
    ssize_t bytes_received;
    time_t t;
    ntp_packet ntp;
    char datetime[64];

    bytes_received = recvfrom(fd,
                buf, sizeof(buf),
                0,
                (struct sockaddr *)&addr, &addrlen);
    if (bytes_received <= 0)
        return;
    ntp = parse_ntp((const unsigned char *)buf, bytes_received);

    t = (time_t)(ntp.tx_tm_s - NTP_TIMESTAMP_DELTA);
    snprintf(datetime, sizeof(datetime), "%s", ctime(&t));
    while (datetime[0] && isspace(datetime[strlen(datetime)-1]))
        datetime[strlen(datetime)-1] = '\0';

    LOG_receiving_from((struct sockaddr*)&addr, addrlen, datetime);
}

static void
send_all_requests(int fd, int argc, char **argv) {
    int i;
    int err;

    /* IPv4/IPv6 co-existence: when doing `getaddrinfo()` lookups below,
     * return addresses in a form that can be used with a IPv4+IPv6
     * combined socket. */
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET6;
    hints.ai_flags = AI_ALL | AI_V4MAPPED;
    hints.ai_socktype = SOCK_DGRAM;

    /*
     * Loop sending requests to all NTP server sockets. This is simply
     * the list of all program parameters.
     */
    for (i=1; i<argc; i++) {
        const char *hostname = argv[i];
        const char *portname = "123";
        struct addrinfo *targets = NULL;
        struct addrinfo *ai;

        /* Ignore anything that looks like an option */
        if (hostname[0] == '-')
            continue;
        /*
         * Uses `getaddrinfo()` to parse the hostname and do a DNS
         * lookup if necessary.
         */
        err = getaddrinfo(hostname, portname, &hints, &targets);
        if (err) {
            fprintf(stderr, "[-] getaddrinfo(): %s\n", gai_strerror(err));
            goto fail;
        }

        /* Each lookup might return multiple targets in a linked-list.
         * Therefore, loop through each item in the linked list. */
        for (ai = targets; ai; ai = ai->ai_next) {

            /* This prints to the command-line the numeric IP address that
             * we are sending to. */
            LOG_sending_to(ai->ai_addr, ai->ai_addrlen);

            /* 
             * This is the heart of the program, where we do the actual sending
             * of packets. Everything else is about getting to this point. You'll
             * want to use a packet-sniffer like Wireshark to look at what's
             * being sent.
             */
            err = sendto(fd, 
                    ntp_req, sizeof(ntp_req), /* packet to send */
                    0,
                    ai->ai_addr, ai->ai_addrlen /* target address */
                    );
            if (err < 0) {
                fprintf(stderr, "[-] sendto() failed: %s\n", strerror(errno));
            }
            
        }

        /* free the linked list created by `getaddrinfo()` */
        freeaddrinfo(targets);
    }
fail:
    ;
}

static void
receive_all_responses(int fd, int timeout) {

    /*
     * Now receive incoming packets. We'll loop waiting for any responses
     * until the timeout expires.
     */
    time_t start = time(0);
    while (time(0) < start + timeout) {
        fd_set readfds;
        struct timeval timeout;
        int count;

        /* This is the standard way for waiting for a response, using
         * select() with a specific timeout value. */
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        count = select(fd+1, &readfds, NULL, NULL, &timeout);
        if (count == 0)
            continue;
        else if (count == -1) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            } else {
                fprintf(stderr, "[-] select(): %s\n", strerror(errno));
                goto fail;
            }
        }

        /* This checks in case there was an error, this shouldn't actually
         * be possible */
        if (!FD_ISSET(fd, &readfds)) {
            int err;
            int so_error = 0;
            socklen_t so_length = sizeof(so_error);
            err = getsockopt(fd, SOL_SOCKET, SO_ERROR, (void*)&so_error, &so_length);
            if (err) {
                fprintf(stderr, "[-] getsockopt(SO_ERROR): %s\n", strerror(errno));
                goto fail;
            }
        }

        /* This function now reads the response and prints the results
         * to the command-line */
        process_response(fd);
    }
fail:
    ;
}
int
main(int argc, char *argv[])
{
    int fd = -1;
 
    /* Usage: provide a list of NTP servers, like "pool.ntp.org"
     * or "time.apple.com" */
    if (argc < 2) {
        const char  *progname = argv[0];
        fprintf(stderr, "[-] usage: %s <ntp-host>...\n", progname);
        return -1;
    }

    /* This sample program reads extra options via ENVIRONMENTAL VARIABLES 
     * instead of PROGRAM ARGUMENTS. You wouldn't set these unless exploring
     * non-default behaviors */
    const char *localaddr = getenv("SOCKDOC_LOCALADDR");
    const char *localport = getenv("SOCKDOC_LOCALPORT");
    int timeout = 10; /* 10 seconds */
    if (getenv("SOCKDOC_TIMEOUT")) {
        timeout = atoi(getenv("SOCKDOC_TIMEOUT"));
        if (timeout <= 0 || 1000000 < timeout) {
            fprintf(stderr, "[-] SOCKDOC_TIMEOUT: bad value %s\n", getenv("SOCKDOC_TIMEOUT"));
            return -1;
        }
        fprintf(stderr, "[+] timeout=%d-seconds\n", timeout);
    }

    /*
     * Create a single half-open socket for UDP communications. We'll use
     * `sendto()` and `recvfrom()` with this socket. This is an IPv4/IPv6
     * agnostic socket, used for both.
     */
    fd = create_udp_socket(localaddr, localport);
    if (fd == -1) {
        fprintf(stderr, "[-] can't create socket\n");
        return 1;
    }

    /*
     * Send NTP requests to all the target listed on the command-line.
     */
    send_all_requests(fd, argc, argv);

    /*
     * Receive all the responses
     */
    receive_all_responses(fd, timeout);

    fprintf(stderr, "[+] done\n");
    close(fd);
    return 0;
}

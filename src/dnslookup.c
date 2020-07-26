#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

void decode_dns_response(const unsigned char *buf, size_t length, unsigned expected_xid)
{
    struct {
        unsigned xid;
        unsigned opcode;
        unsigned status;
        unsigned is_response:1;
        unsigned is_authoritative:1;
        unsigned is_truncated:1;
        unsigned is_recursion_desired:1;
        unsigned is_recursion_available:1;
        unsigned is_authenticated:1;
        unsigned is_authentication_required:1;
        unsigned replycode;
        unsigned qdcount;
        unsigned ancount;
        unsigned nscount;
        unsigned arcount;
    } hdr;

    /*
     * Decode the header
     */
    if (length < 12) {
        fprintf(stderr, "[-] DNS response header too short\n");
        return;
    }

    /* Parse the header fields */
    hdr.xid = buf[0] << 8 | buf[1];
    hdr.is_response = ((buf[2]>>7) & 1);
    hdr.opcode = ((buf[2]>>3) & 0xF);
    hdr.is_authoritative = ((buf[2]>>2) & 1);
    hdr.is_trancated = ((buf[2]>>1) & 1);
    hdr.is_recursion_desired = (buf[2] & 1);
    hdr.is_recursion_available = ((buf[3]>>7) & 1;
    hdr.is_authenticated = ((buf[3]>>5) & 1);
    hdr.is_authentication_required = ((buf[3]>>4) & 1);
    hdr.rcode = buf[3] & 0xF;

    

}

void append_byte(unsigned char *buf, size_t *offset, size_t max, unsigned char value)
{
    if (*offset < max) {
        buf[*offset] = value;
        (*offset)++;
    } else {
        fprintf(stderr, "[-] append_byte: offset=%lu max=%lu\n", (unsigned long)*offset, (unsigned long)max);
    }
}

void append_string(unsigned char *buf, size_t *offset, size_t max, const unsigned char *value, size_t value_length)
{
    size_t i;
    for (i=0; i<value_length; i++)
        append_byte(buf, offset, max, value[i]);
}

void format_query(unsigned char *buf, size_t *length, unsigned *xid, const char *queryname)
{
    size_t i;
    size_t namelength = strlen(queryname);
    size_t offset;
    
    /* Create a transaction ID for matching requests with responses */
    *xid = time(0);
    
    /* The transaction ID will be just the current timestamp */
    if (*length > 12)
        memset(buf, 0, 12);
    buf[0] = (*xid)>>8;
    buf[1] = (*xid) & 0xFF;
    buf[2] = 0x01; /* query, recursion desired */
    buf[5] = 1; /* one query record */
    offset = 12;

    /* A DNS name consists of several labels separated by a dot, like
     * "www.google.com". */
    for (i=0; i<namelength; i++) {
        size_t j;

        /* find the length of the next label */
        for (j=i; j<namelength && queryname[j] != '.'; j++)
            ;
        
        /* If the label is longer than 63 characters, then
         * create an error */
        if (j - i >= 64) {
            fprintf(stderr, "[-] label too long: %.*s\n", (unsigned)(j-i), queryname + i);
            goto error;
        }
        if (j - i == 0) {
            fprintf(stderr, "[-] label empty\n");
            goto error;
        }

        append_byte(buf, &offset, *length, (unsigned char)(j - i));
        append_string(buf, &offset, *length, (const unsigned char *)queryname + i, j - i);
        i = j;
    }

    /* The final label is the value zero */
    append_byte(buf, &offset, *length, 0x00);

    /* Append the record type/class info */
    append_string(buf, &offset, *length, (const unsigned char *)"\x00\x01\x00\x01", 4);

    *length = offset;
    return;
error:
    *length = 0;
    *xid = 0;
}

int main(int argc, char *argv[])
{
    int i;
    const char *queryname = NULL;
    const char *servername = NULL;
    struct addrinfo *addresses = NULL;
    struct addrinfo *ai;
    int err;
    int fd = -1;

    /* Parse the command-line */
    for (i=1; i<argc; i++) {
        if (argv[i][0] == '@')
            servername = argv[i] + 1;
        else
            queryname = argv[i];
    }
    if (servername == NULL || queryname == NULL) {
        fprintf(stderr, "usage:\n dnslookup @<servername> <queryname>\n");
        return 1;
    }

    /* Resolve the IP address of the server, using the built-in 
     * resolver library -- or parse the address if it's given
     * on the command-line */
    err = getaddrinfo(servername, "53", 0, &addresses);
    if (err) {
        fprintf(stderr, "[-] getaddrinfo(): %s\n", gai_strerror(err));
        return -1;
    } else {
        size_t count = 0;
        for (ai = addresses; ai; ai = ai->ai_next)
            count++;
        if (count == 0) {
            fprintf(stderr, "[-] getaddrinfo(): returned zero addresses\n");
            goto cleanup;
        } else {
            fprintf(stderr, "[+] getaddrinfo(): returned %d addresses\n", (int)count);
        }
    }

    for (ai = addresses; ai; ai = ai->ai_next) {
        char addrname[64];
        char portname[8];
        unsigned char buf[65536];
        size_t length = sizeof(buf);
        unsigned xid = 0;
        ssize_t count;
        struct sockaddr_storage sa;
        socklen_t salen = sizeof(sa);

        /* Print the address/port to strings for logging/debugging  */
        err = getnameinfo(ai->ai_addr, ai->ai_addrlen, addrname,
            sizeof(addrname), portname, sizeof(portname),
            NI_NUMERICHOST | NI_NUMERICSERV);
        if (err) {
            fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
            goto cleanup;
        }

        /* Create a socket for interacting */
        fd = socket(ai->ai_family, SOCK_DGRAM, 0);
        if (fd == -1) {
            fprintf(stderr, "[-] socket(): %s\n", strerror(errno));
            goto cleanup;
        }

        /* Set receive timeout */
        {
            struct timeval timeout;      
            timeout.tv_sec = 10;
            timeout.tv_usec = 0;
            err = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
            if (err < 0) {
                fprintf(stderr, "[-] setsockopt(SO_RCVTIMEO): %s\n", strerror(errno));
            }
        }

        /* Format the packet we are doing to send to the target */
        format_query(buf, &length, &xid, queryname);
        if (length == 0)
            goto cleanup;

        /* Send the packet to the target destination */
        count = sendto(fd, buf, length, 0, ai->ai_addr, ai->ai_addrlen);
        if (count < (ssize_t)length) {
            fprintf(stderr, "[-] sendto([%s]:%s): %s\n", addrname, portname,
                strerror(errno));
            close(fd);
            fd = -1;
            continue;
        } else {
            fprintf(stderr, "[+] sendto([%s]:%s): %s\n", addrname, portname,
                "success");
        }

        /* Receive the response */
        count = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr*)&sa, &salen);
        if (count < 0) {
            switch (errno) {
                case EAGAIN:
                    fprintf(stderr, "[-] receive timeout\n");
                    break;
                default:
                    fprintf(stderr, "[-] recvfrom([%s]:%s): %s\n", addrname, portname, strerror(errno));
                    break;
            }
            close(fd);
            fd = -1;
            continue;
        }

    }






cleanup:
    freeaddrinfo(addresses);
    if (fd != -1)
        close(fd);
    return 0;
}

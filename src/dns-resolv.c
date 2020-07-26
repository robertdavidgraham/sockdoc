/*
    Demonstrates using the 'resolv' library to do DNS lookups for records
    other than IP addresses. This is commonly used to lookup MX and SPF
    records for emails, for example.
*/
#include "dns-parse.h"
#include "dns-format.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <resolv.h>
#include <netdb.h>

/**
 * Decode the DNS response, and print it out to the command in
 * the 'presentation' format (the format servers use to read in 
 * records from a text file). This output is similar to the `dig`
 * program. 
 */
static int 
decode_result(const unsigned char *buf, size_t length)
{
    static const size_t sizeof_output = 1024 * 1024;
    char *output = malloc(sizeof_output);
    struct dns_t *dns = NULL;
    int err;
    size_t i;
    
    /* Decode the response */
    dns = dns_parse(buf, length, 0, 0, 0);
    if (dns == NULL || dns->error_code != 0)
        goto fail;

    /* QUESTION */
    if (dns->query_count)
        printf("\n;; QUESTION SECTION:\n");
    for (i=0; i<dns->query_count; i++) {
        dnsrrdata_t *rr = &dns->queries[i];

        printf(";%-23s \t%s\t%-7s %s\n", 
            rr->name,
            (rr->rclass==1)?"IN":"??",
            dns_name_from_rrtype(rr->rtype), 
            "");
    }

    /* ANSWER */
    if (dns->answer_count)
        printf("\n;; ANSWER SECTION:\n");
    for (i=0; i<dns->answer_count; i++) {
        dnsrrdata_t *rr = &dns->answers[i];
        if (rr->rclass != 1)
            continue;
        err = dns_format_rdata(rr, output, sizeof_output);
        printf("%-23s %u\t%s\t%-7s %s\n", 
                rr->name,
                rr->ttl,
                "IN",
                dns_name_from_rrtype(rr->rtype), 
                output);

    }

    /* AUTHORITY */
    if (dns->nameserver_count)
        printf("\n;; AUTHORITY SECTION:\n");
    for (i=0; i<dns->nameserver_count; i++) {
        dnsrrdata_t *rr = &dns->nameservers[i];
        if (rr->rclass != 1)
            continue;
        err = dns_format_rdata(rr, output, sizeof_output);
        printf("%-23s %u\t%s\t%-7s %s\n", 
            rr->name,
            rr->ttl,
            "IN",
            dns_name_from_rrtype(rr->rtype), 
            output);
    }

    /* ADDITIONAL */
    if (dns->additional_count)
        printf("\n;; ADITIONAL SECTION:\n");
    for (i=0; i<dns->additional_count; i++) {
        dnsrrdata_t *rr = &dns->additional[i];
        if (rr->rclass != 1)
            continue;
        if (rr->rtype == 41)
            continue; /* skip EDNS0 */
        err = dns_format_rdata(rr, output, sizeof_output);
        printf("%-23s %u\t%s\t%-7s %s\n", 
                rr->name,
                rr->ttl,
                "IN",
                dns_name_from_rrtype(rr->rtype), 
                output);
    }



    printf("\n");
    return 0;
    
fail:
    fprintf(stderr, "[-] selftest failed\n");
    return 1;
}

static void 
_parse_commandline(int argc, char *argv[], int *type, char **hostname)
{
    if (argc < 2) {
        fprintf(stderr, "usage:\n test-resolv <name>\n");
        exit(1);
    } else {
        int i;
        for (i=1; i<argc; i++) {
            int t = dns_rrtype_from_name(argv[i]);
            if (t == -1) {
                *hostname = argv[i];
            } else {
                *type = t;
            }
        }
    }
    if (*hostname == NULL) {
        fprintf(stderr, "[-] no hostname specified\n");
        exit(1);
    }
}
    
int main(int argc, char *argv[])
{
    unsigned char buf[65536];
    int result;
    char *hostname=NULL;
    int type = 1;

    /* Grab parameters from the command line */
    _parse_commandline(argc, argv, &type, &hostname);
    fprintf(stdout, "; <<>> NotDIG <<>> %s %s\n", dns_name_from_rrtype(type), hostname);

    /* Initialize the built-in DNS resolver library */
    res_init();
#ifdef RES_USE_EDNSO
    _res.options |= RES_USE_EDNSO;
#endif

    /* Do the name resolution. This will block and take a long while */
    errno = 0;
    result = res_search(hostname, 1, type, buf, sizeof(buf));
    if (result < 0) {
        fprintf(stderr, "[-] res_search(): error: %s\n", hstrerror(h_errno));
        return 1;
    }
    fprintf(stderr, "[+] res_search(): %d bytes\n", result);

    /* Now decode the result */
    decode_result(buf, result);

    return 0;
}

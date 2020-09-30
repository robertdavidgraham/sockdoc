#include "util-workers.h"
#include "dns-parse.h"
#include "dns-format.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>

#include <resolv.h>

#ifndef WIN32
#include <netdb.h>
#include <unistd.h>
#endif



int g_debug_level = 0;

void
_debug_list(int argc, char **argv)
{
    int i;
    fprintf(stderr, "{%d %d} ", getpid(), argc);
    for (i=0; i<argc; i++) {
        fprintf(stderr, "%s ", argv[i]);
    }
    fprintf(stderr, "\n");
}

/**
  * Remove leading/trailing whitespace from the string.
 */
static void
_trim(char *line)
{
    while (*line && isspace(line[0] & 0xFF))
        memmove(line, line+1, strlen(line));
    while (*line && isspace(line[strlen(line)-1]))
        line[strlen(line)-1] = '\0';
}


void mywrite_stdout(void *buf, size_t length, void *userdata)
{
    fwrite(buf, 1, length, stdout);
}
void mywrite_stderr(void *buf, size_t length, void *userdata)
{
    fwrite(buf, 1, length, stderr);
}

int
spawn_workers(const char *progname,
              const char *filename,
              unsigned max_children,
              int argc,
              char **argv)
{
    FILE *fp;
    struct workers_t *workers;
    int argc2 = argc+1;
    char **argv2;
    int i;
    
    /* Create a copy of the argument list */
    argv2 = malloc((argc2 + 1) * sizeof(argv2[0]));
    if (argv2 == NULL)
        abort();
    for (i=0; i<argc; i++)
        argv2[i+1] = argv[i];
    argv2[argc2] = NULL;

    /* Initialize a worker subsystem */
    workers = workers_init(&max_children);
    if (workers == NULL) {
        fprintf(stderr, "[-] failed to initialize worker subsystem\n");
        abort();
    }
    
    /* Open the file. If the name is "-", then that means use <stdin>
     * instead of a file */
    if (filename == NULL)
        fp = NULL; /* skip file, do only command-line */
    else if (strcmp(filename, "-") == 0)
        fp = stdin; /* instead of file, read stdin */
    else {
        fp = fopen(filename, "rt");
        if (fp == NULL) {
            char buf[512];
            fprintf(stderr, "[-] %s: %s\n", filename, strerror(errno));
            if (getcwd(buf, sizeof(buf)))
				fprintf(stderr, "[-] cwd = %s\n", buf);
            return 1;
        }
    }
    
    /*
     * Keep spawning workers as we parse the file
     */
    while (fp) {
        char hostname[1024];
        
        /* Get the next line of text from the file */
        if (fgets(hostname, sizeof(hostname), fp) == NULL)
            break;
        
        /* Remove leading/trailing whitespace */
        _trim(hostname);
        
        /* Ignore empty lines or comments */
        if (hostname[0] == '\0' || ispunct(hostname[0]))
            continue;

        /* Now spawn the child */
        argv2[0] = hostname;
        workers_spawn(workers, progname, argc2, argv2);

        /* Do this at least once, which slows down how fast we spawn
         * new processes. If we've reached the maximum children count,
         * then we stay stuck here processing children until one
         * of them exits and creates room for a new child */
        do {
            int closed_count = workers_read(workers, 100, mywrite_stdout, mywrite_stderr, 0);
            if (closed_count) {
                workers_reap(workers);
            }
        } while (workers_count(workers) == max_children);
    }
    if (fp && g_debug_level)
        fprintf(stderr, "[+] done reading file\n");
    
    
    /* We've run out of entries in the file, but we still may have
     * child processes in various states of execution, so we sit
     * here waiting for them all to exit */
    while (workers_count(workers)) {
        int closed_count = workers_read(workers, 100, mywrite_stdout, mywrite_stderr, 0);
        if (closed_count) {
            workers_reap(workers);
        }
    }
    
    /* Clean up any remaining pipe stuff */
    workers_read(workers, 100, mywrite_stdout, mywrite_stderr, 0);

    /* There are no more children left, so now it's time to exit */
    return 0;
}


/**
 * Uses realloc() to expand a buffer we are appending to with snprintf()
 * calls.
 */
static char *
snprintf_append(char *buf, size_t *length, const char *fmt, ...)
{
    va_list marker;
    int new_count;

    /* Discover how many more bytes we need */
    va_start(marker, fmt);
    new_count = vsnprintf(buf + *length, 0, fmt, marker);
    va_end(marker);
    if (new_count < 0)
        return buf;

    /* Expand buffer by that many bytes */
    buf = realloc(buf, *length + new_count + 1);
    
    /* Now do the actual printf() */
    va_start(marker, fmt);
    new_count = vsnprintf(buf + *length, *length + new_count + 1, fmt, marker);
    va_end(marker);

    /* Increase the length by that number of bytes */
    *length += new_count;
    
    /* Return the newly allocated buffer to replace the old one */
    return buf;
}

/**
 * Decode the DNS response, and print it out to the command in
 * the 'presentation' format (the format servers use to read in 
 * records from a text file). This output is similar to the `dig`
 * program. 
 */
static int 
decode_result(const char *hostname, const unsigned char *buf, size_t length)
{
    struct dnsparse_ctx_t dns;
    struct dnsrr_t rr;
    int err;
    int section = -1;
    char *result = NULL;
    size_t result_length = 0;

    /* Start decoding this DNS data */
    err = dns_start(&dns, buf, length);
    if (err)
        goto fail;
    
    /* Print all the records */
    for (;;) {
        char data[16384];
        dnsrrdata_t rdata;

        data[0] = 0;

        /* Get the next record */
        err = dns_next_rr(&dns, &rr);
        if (err)
            goto fail;

        /* Print the section header like DIG */
        if (section != rr.section) {
            static const char *section_name[] = {"QUESTION", "ANSWER", "AUTHORITY", "ADDITIONAL"};
            section = rr.section;
            result = snprintf_append(result, &result_length, ";; %s SECTION:\n", section_name[rr.section]);
        }

        /* Format the resource-record */
        /* If in the QUERY section, then skip this */
        if (rr.section == 0) {
            result = snprintf_append(result, &result_length, ";%-23s \t%s\t%-7s %s\n",
                rr.name,
                (rr.opt_class==1)?"IN":"??",
                dns_name_from_rrtype(rr.opt_type), 
                data);
        
            continue;
        }

        /* If not "IN" class, then skip this */
        if (rr.opt_class != 1)
            continue;

        /* If ENDS0, then skip this */
        if (rr.opt_type == 41)
            continue;
        
        /* Decode the record  */
        err = dns_parse_rr(&dns, rr.rdata, rr.rdlength, rr.opt_type, &rdata);
        if (err < 0) {
            fprintf(stderr, "[-] %s: dns_parse_rr(%s) failed, len=%u\n", hostname, dns_name_from_rrtype(rr.opt_type), (unsigned)rr.rdlength);
            goto fail;
        }

        /* Convert the record to "presentation" format, so that we can do comparisons 
         * on it */
        if (err > 0) {
            /* An unknown type was encountered in the parse, so do generic
             * formatting. */
            dns_format_rdata_generic(rr.rdata, rr.rdlength, data, sizeof(data));
        } else {
            err = dns_format_rdata(rr.opt_type, &rdata, data, sizeof(data));
            if (err) {
                fprintf(stderr, "[-] %s: unknown typed: %u\n", hostname, rr.opt_type);
            }
        }

        /* Print in presentation format, so that it can be imported into a web server.
         * Should be almost identical to dig. */
        result = snprintf_append(result, &result_length, "%-23s %u\t%s\t%-7s %s\n",
                rr.name,
                rr.opt_ttl,
                (rr.opt_class==1)?"IN":"??",
                dns_name_from_rrtype(rr.opt_type), 
                data);
        
        if (dns.offset >= dns.length)
            break;
    }


    /* Write all the output as a single step, so that many programs
     * writing at the same time don't interleave results */
    result = snprintf_append(result, &result_length, "\n");
    fwrite(result, 1, result_length, stdout);
    free(result);
    return 0;
    
fail:
    fprintf(stderr, "[-] %s: parse failed\n", hostname);
    return 1;
}

static void 
_parse_commandline(int argc, char *argv[], int *type, char **hostname, char **filename, int *verbose_level, int *workers)
{
    if (argc < 2) {
        fprintf(stderr, "usage:\n test-resolv <name>\n");
        exit(1);
    } else {
        int i;
        for (i=1; i<argc; i++) {
            if (argv[i][0] == '-') 
            switch (argv[i][1]) {
            case 'f':
                if (argv[i][2] == '\0') {
                    *filename = argv[++i];
                } else
                    *filename = argv[i]+2;
                if (i == argc || *filename == NULL || **filename == '\0') {
                    fprintf(stderr, "[-] expected filename after '-f'\n");
                    exit(1);
                }
                break;
            case 'w':
                if (argv[i][2] == '\0') {
                    *workers = (int)strtoul(argv[++i], 0, 0);
                } else
                    *workers = (int)strtoul(argv[i]+2, 0, 0);
                if (i == argc) {
                    fprintf(stderr, "[-] expected workers after '-w'\n");
                    exit(1);
                }
                if (*workers == 0) {
                    fprintf(stderr, "[-] worker count invalid, must be number [1...10000]\n");
                    exit(1);
                }
                break;
            case 'd':
            case 'v':
                {
                    size_t j;
                    for (j=1; argv[i][j]; j++) {
                        if (argv[i][j] == 'd')
                            g_debug_level++;
                        if (argv[i][j] == 'v')
                            (*verbose_level)++;
                    }
                }
                break;
            case 'q':
                verbose_level = 0;
                break;
            default:
                fprintf(stderr, "[-] unknown parameter '-%c'\n", argv[i][1]);
                exit(1);
            } else {
                int t = dns_rrtype_from_name(argv[i]);
                if (t == -1) {
                    *hostname = argv[i];
                } else {
                    *type = t;
                }
            }
        }
    }
    if (*hostname == NULL && *filename == NULL) {
        fprintf(stderr, "[-] no filename specified\n");
        exit(1);
    }
}

static int
main_resolve_host(int type, const char *hostname, int verbose_level)
{
    unsigned char buf[65536];
    int result;
    
    /* Initialize the built-in DNS resolver library */
    res_init();

    /* Do the name resolution. This will block and take a long while */
    result = res_query(hostname, 1, type, buf, sizeof(buf));
    if (result < 0) {
        fprintf(stderr, "[-] %s: %s\n", hostname, hstrerror(h_errno));
        return 1;
    } else if (g_debug_level > 1) {
        fprintf(stderr, "[+] %s: success\n", hostname);
    }
    
    /* Now decode the result */
    decode_result(hostname, buf, result);

    return 0;
}

char **_copy_parameters(int argc, char *argv[])
{
    int i;
    char **argv2 = NULL;
    
    for (i=0; i<argc; i++) {
        argv2 = realloc(argv2, (i + 2) * sizeof(*argv2));
        argv2[i] = strdup(argv[i]);
    }
    argv2[i] = NULL; /* nul termiante the list */
    return argv2;
}

char **_strip_parameter(int *argc, char *argv[], const char *parm, int is_additional)
{
    int i;

    for (i=0; i<*argc; i++) {
        /* Remove 'filename' parameter from the list */
        if (strlen(argv[i]) >= strlen(parm) && memcmp(argv[i], parm, strlen(parm)) == 0) {
            if (is_additional && argv[i][strlen(parm)] == '\0') {
                //fprintf(stderr, "stripping '%s' and '%s'\n", argv[i], argv[i+1]);
                memmove(argv+i, argv+i+1, ((*argc) - i) * sizeof(argv[0]));
                (*argc)--;
            } else {
                fprintf(stderr, "stripping '%s'\n", argv[i]);
            }
            memmove(argv+i, argv+i+1, ((*argc) - i) * sizeof(argv[0]));
            (*argc)--;
            i--;
            continue;
        }
    }
    return argv;
}

int main(int argc, char *argv[])
{
    char *hostname = NULL;
    char *filename = NULL;
    int type = 1;
    int verbose_level = 0;
    int workers = 10;

    //_debug_list(argc, argv);
    /* Grab parameters from the command line */
    _parse_commandline(argc, argv, &type, &hostname, &filename, &verbose_level, &workers);
    
    if (hostname) {
        if (g_debug_level > 1) {
            fprintf(stderr, "[ ] %s: type=%s\n", hostname, dns_name_from_rrtype(type));
        }
        /* We are a child program, so just do the resolution */
        return main_resolve_host(type, hostname, verbose_level);
    } else {
        /* We are the parent program, so read a file and spawn
         * programs */
        char **argv2;
        int argc2 = argc;

        /* Create a copy of whatever parameters were passed in, other than the filename */
        argv2 = _copy_parameters(argc, argv);
        _strip_parameter(&argc2, argv2, "-f", 1);
        
        if (g_debug_level) {
            fprintf(stderr, "[ ] type=%s filename=%s\n", dns_name_from_rrtype(type), filename);
            fprintf(stderr, "[ ] workers = %d\n", workers);
        }
        
        return spawn_workers(argv[0],
                    filename,
                    workers,
                    argc2-1,
                    argv2+1);
    }

    return 0;
}


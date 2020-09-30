#include "parse-http.h"
#include "util-ctype.h"
#include "util-malloc.h"
#include "util-smack.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

struct uriprefix {
    char *prefix;
    size_t length;
};
struct httpparser {
    struct SMACK *ac_methods;
    struct SMACK *ac_prefixes;

    struct {
        size_t count;
        struct uriprefix *list;
    } uri;
};


/*****************************************************************************
 *****************************************************************************/
struct httpparser *
httpparser_create(void)
{
    struct httpparser *parser;

    /* Create a parser structure */
    parser = malloc(sizeof(*parser));
    if (parser == NULL)
        return NULL;
    memset(parser, 0, sizeof(*parser));

    return parser;
}




/*****************************************************************************
 *****************************************************************************/
void
httpparser_compile(struct httpparser *p)
{
    unsigned flags = SMACK_ANCHOR_BEGIN | SMACK_ANCHOR_END;
    size_t i;


    /* Add methods */
    p->ac_methods = smack_create("methods", 1);
    smack_add_pattern(p->ac_methods, "CONNECT", 0, METHOD_CONNECT, flags);
    smack_add_pattern(p->ac_methods, "DELETE", 0, METHOD_DELETE, flags);
    smack_add_pattern(p->ac_methods, "GET", 0, METHOD_GET, flags);
    smack_add_pattern(p->ac_methods, "HEAD", 0, METHOD_HEAD, flags);
    smack_add_pattern(p->ac_methods, "OPTIONS", 0, METHOD_OPTIONS, flags);
    smack_add_pattern(p->ac_methods, "PATCH", 0, METHOD_PATCH, flags);
    smack_add_pattern(p->ac_methods, "POST", 0, METHOD_POST, flags);
    smack_add_pattern(p->ac_methods, "PUT", 0, METHOD_PUT, flags);
    smack_add_pattern(p->ac_methods, "TRACE", 0, METHOD_TRACE, flags);
    /* Nginx: PROPPATCH, PROPFIND, UNLOCK, MKCOL, LOCK, MOVE, COPY, */
    /* others: http://webconcepts.info/concepts/http-method/*/
    smack_compile(p->ac_methods);

    /* Add header field parsers */
    
    
    /* Add URL prefixes */
    p->ac_prefixes = smack_create("uris", 1);
    for (i = 0; i < p->uri.count; i++) {
        smack_add_pattern(p->ac_prefixes, p->uri.list[i].prefix,
            p->uri.list[i].length, i,
            SMACK_ANCHOR_BEGIN | SMACK_ANCHOR_END);
    }
    smack_compile(p->ac_methods);
}

/*****************************************************************************
 *****************************************************************************/
unsigned
httpparser_register_url_prefix(
    struct httpparser *parser, unsigned id, const char *uri, size_t length)
{
    struct uriprefix *p;

    if (length == 0 && uri != 0)
        length = strlen(uri);

    parser->uri.list = REALLOCARRAY(
        parser->uri.list, parser->uri.count + 1, sizeof(parser->uri.list[0]));
    p = &parser->uri.list[parser->uri.count++];
    p->length = length;
    p->prefix = MALLOCDUP(uri, length);

    return (unsigned)parser->uri.count - 1;
}

/*****************************************************************************
 *****************************************************************************/
void
httpparse_begin(struct httpparser *parser, struct httpheader *hdr)
{
    memset(hdr, 0, sizeof(*hdr));
}

int
httpparse_next_uri(
    const struct httpparser *parser, struct httpheader *hdr, unsigned char c)
{
    /*id = smack_search_next(parser->ac_prefixes, &hdr->state2, &c, 0, 1);

    printf("%c", c);*/
    return 0;
}

/***************************************************************************
 ***************************************************************************/
static unsigned
hexval(char c)
{
    if ('0' <= c && c <= '9')
        return (unsigned)(c - '0');
    else if ('a' <= c && c <= 'f')
        return (unsigned)(c - 'a' + 10);
    else if ('A' <= c && c <= 'F')
        return (unsigned)(c - 'A' + 10);
    else
        return 0xFF;
}

/***************************************************************************
 ***************************************************************************/
void
httpparse_start(const struct httpparser *parser, struct httpheader *hdr)
{
    memset(hdr, 0, sizeof(*hdr));
}

/***************************************************************************
 ***************************************************************************/
int
httpparse_next(
    const struct httpparser *parser, struct httpheader *hdr, unsigned char c)
{
    unsigned next_state = hdr->state1;
    enum {
        SPACE0,
        METHOD,
        SPACE1,
        URI,
        URL_PERCENT1,
        URL_PERCENT2,
        SPACE2,
        VERSION_H,
        VERSION_HT,
        VERSION_HTT,
        VERSION_HTTP,
        VERSION_HTTPMAJ,
        VERSION_HTTPMIN,
        VERSION_ERR,
        EOL,
        NAME,
        VALUE,

    };

    switch (next_state) {
    case SPACE0:
        hdr->state2 = 0;
        if (ISSPACE(c))
            break;
        /* fall through */
    case METHOD:
        if (c == '\n') {
            next_state = EOL;
            break;
        }
        if (ISSPACE(c)) {
            hdr->method = smack_search_done(parser->ac_methods, &hdr->state2);
            next_state = SPACE1;
        } else {
            smack_search_next(parser->ac_methods, &hdr->state2, &c, 0, 1);
            next_state = METHOD;
        }
        break;
    case SPACE1:
        if (c == '\n') {
            next_state = EOL;
            break;
        }
        if (ISSPACE(c))
            break;
        next_state = URI;
        /* fall through */
    case URI:
        switch (c) {
        case '/':
        default:
            httpparse_next_uri(parser, hdr, c);
            break;
        case '+':
            httpparse_next_uri(parser, hdr, ' ');
                break;
        case '%':
            next_state = URL_PERCENT1;
            break;
        case ' ':
        case '\t':
        case '\r':
            next_state = SPACE2;
            break;
        case '\n':
            next_state = EOL;
            break;
        }
        break;
    case URL_PERCENT1:
        if (ISXDIGIT(c)) {
            hdr->tmp = hexval(c) << 4;
            next_state = URL_PERCENT2;
        } else {
            hdr->is_error = true;
            if (c == '\n')
                next_state = EOL;
            else
                next_state = URI;
        }
        break;
    case URL_PERCENT2:
        if (ISXDIGIT(c)) {
            hdr->tmp |= hexval(c);
            next_state = URI;
        } else {
            if (c == '\n')
                next_state = EOL;
            else
                next_state = URI;
        }
        break;
    case SPACE2:
            switch (c) {
                case '\n':
                    next_state = EOL;
                    break;
                case '\t':
                case '\r':
                case ' ':
                    next_state = SPACE2;
                    break;
                case 'h':
                case 'H':
                    next_state = VERSION_H;
                    break;
                default:
                    next_state = VERSION_ERR;
            }
            break;
        case VERSION_ERR:
            if (c == '\n')
                next_state = EOL;
            break;
        case VERSION_H:
            switch (c) {
                case '\n':
                    next_state = EOL;
                    break;
                case 't':
                case 'T':
                    next_state = VERSION_HT;
                    break;
                default:
                    next_state = VERSION_ERR;
                    break;
            }
            break;
        case VERSION_HT:
            switch (c) {
                case '\n':
                    next_state = EOL;
                    break;
                case 't':
                case 'T':
                    next_state = VERSION_HTT;
                    break;
                default:
                    next_state = VERSION_ERR;
                    break;
            }
            break;
        case VERSION_HTT:
            switch (c) {
                case '\n':
                    next_state = EOL;
                    break;
                case 'p':
                case 'P':
                    next_state = VERSION_HTTP;
                    break;
                default:
                    next_state = VERSION_ERR;
                    break;
            }
            break;
        case VERSION_HTTP:
            switch (c) {
                case '\n':
                    next_state = EOL;
                    break;
                case '/':
                    next_state = VERSION_HTTPMAJ;
                    break;
                default:
                    next_state = VERSION_ERR;
                    break;
            }
            break;
        case VERSION_HTTPMAJ:
            switch (c) {
                case '\r':
                    break;
                case '\n':
                    next_state = EOL;
                    break;
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                    if (hdr->version_major < 100)
                        hdr->version_major = hdr->version_major * 10 + (c - '0');
                    else {
                        hdr->version_major = 0;
                        next_state = VERSION_ERR;
                    }
                    break;
                case '.':
                    next_state = VERSION_HTTPMIN;
                    break;
                default:
                    next_state = VERSION_ERR;
                    break;
            }
            break;
        case VERSION_HTTPMIN:
            switch (c) {
                case '\r':
                    break;
                case '\n':
                    next_state = EOL;
                    break;
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                    if (hdr->version_minor < 100)
                        hdr->version_minor = hdr->version_minor * 10 + (c - '0');
                    else {
                        hdr->version_minor = 0;
                        next_state = VERSION_ERR;
                    }
                    break;
                default:
                    next_state = VERSION_ERR;
                    break;
            }
            break;
    case EOL:
            break;
            

    default:
            printf("err\n");
    }

    hdr->state1 = next_state;
    return 0;
}


          

/***************************************************************************
 ***************************************************************************/
int
httpparser_selftest(void)
{
    size_t i;
    struct httpparser *parser;
    struct httpheader hdr;
    static const char sample[]
        = "GET / HTTP/1.1\r\n"
          "Host: www.nytimes.com\r\n"
          "Connection: keep-alive\r\n"
          "Upgrade-Insecure-Requests: 1\r\n"
          "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_14_5) "
          "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/76.0.3809.100 "
          "Safari/537.36\r\n"
          "DNT: 1\r\n"
          "Accept: "
          "text/html,application/xhtml+xml,application/xml;q=0.9,image/"
          "webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3\r\n"
          "Accept-Encoding: gzip, deflate\r\n"
          "Accept-Language: en-US,en;q=0.9\r\n"
          "Cookie: nyt-a=Xa6aiXfxMmO-BS3Uf_LJoS; "
          "optimizelyEndUserId=oeu1546063050462r0.5510475026965527\r\n"
          "\r\n";

    parser = httpparser_create();
    httpparser_register_url_prefix(parser, 1, "/index.html", 0);
    httpparser_register_url_prefix(parser, 2, "/cgi-bin", 0);
    httpparser_compile(parser);

    httpparse_start(parser, &hdr);
    for (i = 0; sample[i]; i++) {
        int err;
        err = httpparse_next(parser, &hdr, sample[i]);
        if (err == 1) {
            printf("done\n");
            return 1;
        }
    }

    return 0;
}

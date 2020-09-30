#include "parse-http-fields.h"
#include "parse-http.h"
#include "util-malloc.h"
#include "util-ctype.h"


static void
_field_append(struct httpheader *hdr, unsigned char c)
{
    hdr->buf[hdr->offset++] = c;
}

static size_t
_field_length(struct httpheader *hdr)
{
    return hdr->length - hdr->offset;
}
static size_t
_field_init(struct httpheader *hdr)
{
    return hdr->offset;
}

/*****************************************************************************
 *****************************************************************************/
int
http_parse_host(const struct httpparser *p, struct httpheader *hdr, unsigned char c)
{
    unsigned next_state = hdr->state2;
    enum {
        HOST_START,
        HOST_TEXT,
        HOST_TEXT_SPACE,
        HOST_IPV6,
        HOST_IPV6_SPACE,
        HOST_PORT,
        HOST_PORT_SPACE,
        HOST_ERROR = ~0
    };
    /* make sure we have enough buffer space to hold the host field */
    if (hdr->offset >= hdr->length) {
        hdr->length = hdr->length * 2 + 1;
        hdr->buf = REALLOC(hdr->buf, hdr->length);
    }
    
    switch (next_state) {
        case HOST_START:
            if (hdr->host.offset || hdr->host.length) {
                /* reject headers with two Host:fields */
                hdr->is_error = 1;
                next_state = HOST_ERROR;
                break;
            }
            switch (c) {
                case '\n':
                    hdr->is_error = 1;
                    break;
                case '[':
                    hdr->host.offset = _field_init(hdr);
                    _field_append(hdr, c);
                    next_state = HOST_IPV6;
                    break;
                default:
                    hdr->host.offset = _field_init(hdr);
                    _field_append(hdr, c);
                    next_state = HOST_TEXT;
                    break;
            }
            break;
        case HOST_TEXT:
            /* TODO: for now, just copy the field up to 256 bytes*/
            switch (c) {
                case '\n':
                    hdr->host.length = _field_length(hdr);
                    break;
                case ':':
                    next_state = HOST_PORT;
                    break;
                case ' ':
                case '\r':
                case '\t':
                    hdr->host.length = _field_length(hdr);
                    next_state = HOST_TEXT_SPACE;
                    break;
                default:
                    _field_append(hdr, c);
                    break;
            }
            break;
        case HOST_TEXT_SPACE:
            switch (c) {
                case '\n':
                    break;
                case ':':
                    next_state = HOST_PORT;
                    break;
                case ' ':
                case '\r':
                case '\t':
                    next_state = HOST_TEXT_SPACE;
                    break;
                default:
                    /* non-space, non-colon at this point is an error */
                    hdr->is_error = 1;
                    break;
            }
            break;
        case HOST_PORT:
            switch (c) {
                case '\n':
                    break;
                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9':
                    hdr->host_port = hdr->host_port * 10 + c - '0';
                    if (hdr->host_port > 65535)
                        hdr->is_error = 1;
                    next_state = HOST_PORT;
                    break;
                case ' ':
                case '\r':
                case '\t':
                    next_state = HOST_PORT_SPACE;
                    break;
                default:
                    _field_append(hdr, c);
                    break;
            }
            break;
        case HOST_PORT_SPACE:
            if (!ISSPACE(c))
                hdr->is_error = 1;
            break;
        case HOST_ERROR:
        default:
            hdr->is_error = 1;
            next_state = HOST_ERROR;
            break;
    }
    hdr->state2 = next_state;
    return 0;
}


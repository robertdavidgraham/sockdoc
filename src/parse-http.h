#ifndef PARSE_HTTP_H
#define PARSE_HTTP_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>
#include <stdbool.h>

enum Methods {
    METHOD_CONNECT=1, METHOD_DELETE, METHOD_GET, METHOD_HEAD, METHOD_OPTIONS,
    METHOD_PATCH, METHOD_POST, METHOD_PUT, METHOD_TRACE
};

int httpparser_selftest(void);

struct httpheaderfield {
    size_t offset;
    size_t length;
};
    
/**
 * This structure represents a parsed HTTP request header, either during the
 * parsing, or as the result from having parsed the header.
 */
struct httpheader {
    /** The state-machine variable while parsing the HTTP header structure,
     * such as states for the method, URL, version, and so on. Set to
     * zero (0) as the initial state before parsing */
    unsigned state1;
    
    /** The state-machine variable for parsing individual header fields.
     * Set to zero (0) to start parsing the field (after the field name,
     * colon, and leading space). A value of (~0) indicates an error
     * occured while parsing the state */
    unsigned state2;
    
    /**
     * The method (GET, PUT, HEAD, etc.) for the HTTP request. Values
     * from 1-1023 indicate standard methods, while values above 1024
     * indicate custom methods set with `httpparser_add_method()`.
     */
    int method;
    
    /**
     * The major version, which should really be either 0 or 1, for
     * HTTP/0.9, HTTP/1.0, HTTP/1.1 */
    unsigned char version_major;
    
    /**
     * The parsed minor version
     */
    unsigned char version_minor;
    
    /**
     * Whether an error occurred while parsing the HTTP header. When
     * this flag is set, the header should be discarded.
     */
    bool is_error;
    
    unsigned tmp;
    
    /**
     * The parsed host field.
     */
    struct httpheaderfield host;
    unsigned host_port;
    
    char *buf;
    size_t offset;
    size_t length;
};

#ifdef __cplusplus
}
#endif
#endif

/*
    Wrappers around the malloc()/heap functions to `abort()` the program in
    case of running out of memory.
 
    Adds a REALLOCARRAY() function that checks for integer
    overflow before trying to allocate memory. This is a typical
    function that while technically not standard, is often found
    in libraries.

    Adds a MALLOCDUP() function that duplicates an object, simply
    a malloc()/memcpy() pair.
*/
#ifndef UTIL_MALLOC_H
#define UTIL_MALLOC_H
#include <stdio.h>
#include <stdlib.h>

void *
REALLOCARRAY(void *p, size_t count, size_t size);

void *
CALLOC(size_t count, size_t size);

void *
MALLOC(size_t size);

void *
REALLOC(void *p, size_t size);

char *
STRDUP(const char *str);

void *
MALLOCDUP(const void *p, size_t size);


#endif

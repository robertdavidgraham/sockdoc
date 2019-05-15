/* Copyright (c) 2019 - by Robert David Graham
 * MIT License: https://github.com/robertdavidgraham/sockdoc/LICENSE */
/*
   Allocates from the operating system secured memory for storying
   passwords and crypto keys
*/
#ifndef UTIL_SECMEM_H
#define UTIL_SECMEM_H
#include <stdio.h>

/**
 * Allocates from the operating system a region of "secure" memory
 * for the holding of things like passwords and encryption keys.
 * This secure memory:
 *   - won't be paged to swap files
 *   - won't be dumped to core files upon crashes
 *   - won't be inheritied with child processes
 *   - won't be shareable with other processes
 *   - is surrounded by guard pages, so can't be accidentally read/written
 * The matching 'secmem_free()' must be called to free the memory, the
 * resulting pointer cannot be manipulated by normal memory allocation
 * functions.
 * @param size
 *  The number of bytes to allocate.
 * @return
 *  A pointer to the memory, or NULL if an error occured.
 */
void *util_secmem_alloc(size_t size);

/**
 * Free a previously allocated region of secure memory. The contents will
 * be wiped before returned to the operating system, so there is no need
 * for the user to clear the contents themselves.
 * @param p
 *      This is the pointer returned by the call to 'secmem_alloc()'.
 */
void util_secmem_free(void *p);

/**
 * Calls memset(0) on the memory, but avoids compiler optimizations that
 * might remove such calls. Smart compilers notice that we don't use the
 * contents of memory after a memset(), and thus may remove it. But for
 * security, we really do want to wipe the memory. This function can
 * be used with any pointer, not just the other functions defined in this
 * module.
 * @param p
 *  The memory being wiped, set to zero.
 * @param size
 *  The number of bytes to wipe.
 */
void util_wipe(volatile void *p, size_t size);

#endif

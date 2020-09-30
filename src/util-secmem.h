/*
    "Security protected memory allocation for holding passwords/keys"

    Copyright: 2019 by Robert David Graham
    Authors: Robert David Graham
    License: MIT
      https://github.com/robertdavidgraham/sockdoc/blob/master/src/LICENSE
    Dependencies: operating system calls

 This module implements "secure memory" for storing security-critical
 information such as passwords and crypto keys. This shouldn't be
 used for normal memory allocation as it's slow, inefficient, and 
 will quickly exhaust operating system resources if too much is allocated.

 The protections it provides is by allocating pages directly form the
 operating system and marking them as something that should not appear
 in swap or core files, or be shared with child processes. They are
 surrounded with guard pages so that buffer/memory overruns won't
 accident change or read them.

 There are also also the alternative compare and clear/memset functions
 to handle common problems with memcmp() and memset().
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
 * Compare two chuncks of memory in constant time. The normal `memcmp()`
 * function stops as soon as it detects a difference, which allows for
 * a timing attack where the adversay can measure the difference.
 * This function is much slower than `memcmp()`, so should only
 * be used in cryptographic cases.
 */
int util_secmem_memcmp(void *lhs, void *rhs, size_t length);

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
void util_secmem_wipe(volatile void *p, size_t length);

/**
 * A quick sanity check of this module. This is not a comprehensive unit
 * test, but verifies basic functionality with a small/quick test that
 * won't bloat the size of executables, and can be called at startup
 * by users of this program without delaying the program.
 * @return 
 *    1 on success, 0 on failure
 */
int util_secmem_selftest(void);

#endif

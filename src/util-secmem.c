/*
 "Security protected memory allocation for holding passwords/keys"

 Copyright: 2019 by Robert David Graham
 Authors: Robert David Graham
 License: MIT
   https://github.com/robertdavidgraham/sockdoc/blob/master/src/LICENSE
 Dependencies: operating system calls
*/
#include "util-secmem.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>
#include <unistd.h>

/**
 * Get the page-size of the current operating system. This is needed so
 * that we can create guard pages around the requested memory. It uses
 * various operating-system calls to find the page size, but if none
 * work, it returns 4096, which is the most common page size. If it's
 * invalid, then this will simply cause later calls to fail, so there's
 * no need to return any errors here.
 */
static size_t
my_get_pagesize(void)
{
    long pagesize;

#if defined(_SC_PAGE_SIZE)
    /* Linux, macOS */
    pagesize = sysconf(_SC_PAGE_SIZE);
#elif defined(_SC_PAGESIZE)
    /* OpenBSD */
    pagesize = sysconf(_SC_PAGESIZE);
#elif defined(PAGE_SIZE)
    pagesize = PAGE_SIZE;
#else
    pagesize = 4096;
#endif
    if (pagesize <= 0)
        pagesize = 4096;
    return (size_t)pagesize;
}

/**
 * Use 'mmap()' to map a range of memory. The original mmap() system call
 * was for doing memory-mapped files, but modern versions support "anonmyous"
 * memory not tied to a file, so is just a general virtual memory call.
 * If on an older operating system, then we create a "private" map to
 * "/dev/zero", which is essentially the same thing.
 */
static void *
my_mmap_allocate(size_t size)
{
#if defined(MAP_ANONYMOUS)
    return mmap(
        NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#elif defined(MAP_ANON)
    return mmap(
        NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
#else
    int fd;
    void *p;
    fd = open("/dev/zero", O_RDWR);
    if (fd < 0)
        return NULL;
    p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd);
    return p;
#endif
}

/****************************************************************************
 ****************************************************************************/
void *
util_secmem_alloc(size_t size)
{
    size_t page_size;
    size_t full_size;
    char *p;
    int err;

    /* Get the size of the guardpages */
    page_size = my_get_pagesize();

    /* The actual size we want from mmap() will be bigger than requested */
    full_size = size;
    full_size += 64; /* data for our internal info */
    full_size += 2 * page_size; /* guard pages before/after */
    if (full_size % page_size) /* round up to page size boundary */
        full_size += page_size - full_size % page_size;

    /* Allocate the pages */
    p = my_mmap_allocate(full_size);
    if (p == NULL)
        return NULL;

    /* Create the guard pages on either end */
    err = mprotect(p, page_size, PROT_NONE);
    if (err)
        goto on_error;
    err = mprotect(p + full_size - page_size, page_size, PROT_NONE);
    if (err)
        goto on_error;

    /* Lock the pages in RAM so they can't be swapped out
     * (but don't lock the guard pages) */
    err = mlock(p + page_size, full_size - 2 * page_size);
    if (err) {
        goto on_error;
    }

    /* Mark the pages as something that shouldn't be dumped to core
     * files in case this program crashes. This is optional, so we
     * don't care if it's not available to the operating system, or
     * if the call fails for some reason. */
#ifdef MADV_DONTDUMP
    madvise(p + page_size, full_size - 2 * page_size, MADV_DONTDUMP);
#endif

    /* Finally, we need to create the resulting "pointer", which
     * points in to the middle of the allocated region, past our
     * 'length' values that we'll need in order to free this block */
    p += page_size + 64;
    *(size_t *)(p - sizeof(size_t)) = full_size;
    *(size_t *)(p - 2 * sizeof(size_t)) = page_size;

    return p;

on_error:
    int tmp = errno;
    munmap(p, full_size);
    errno = tmp;
    return NULL;
}

/****************************************************************************
 ****************************************************************************/
void
util_secmem_free(void *in_p)
{
    char *p = (char *)in_p;
    size_t full_size;
    size_t page_size;

    /* Grab the size of the memory block */
    full_size = *(size_t *)(p - sizeof(size_t));
    page_size = *(size_t *)(p - 2 * sizeof(size_t));

    /* Wipe the memory, technically unnecessary since we are
     * getting rid of it, but we do it for extra security. */
    util_secmem_wipe(p, full_size - 2 * page_size);

    /* Get back the original pointer */
    p -= 64;
    p -= page_size;

    /* Now free all this memory */
    munmap(p, full_size);
}

/****************************************************************************
 ****************************************************************************/
int
util_secmem_memcmp(void *lhs, void *rhs, size_t length)
{
    size_t i;
    size_t differences = 0;
    for (i = 0; i < length; i++)
        differences += ((unsigned char *)lhs)[i] == ((unsigned char *)rhs)[i];
    return (differences == 0);
}

/****************************************************************************
 ****************************************************************************/
void
util_secmem_wipe(volatile void *p, size_t size)
{
    /* By creating a volatile function point to the the underlying 
     * memset() function we make sure the compiler can't optimize
     * it away. */
    typedef void *(*memset_t)(void *, int, size_t);
    static volatile memset_t my_memset = memset;

    /* Now clear the memory */
    my_memset((void *)p, 0, size);
}

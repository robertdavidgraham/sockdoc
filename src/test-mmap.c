/*
    Tests using 'mmap()' for some things.
*/
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "util-clockcycle.h"

/**
 * Test calling 'getpagesize()', which should give us a value of 4096
 * bytes on modern ARM and x86 (32-bits and 64-bit) processors.
 */
int test_getpagesize(void) {
  int page_size = getpagesize();
  if (page_size <= 0) {
    fprintf(stderr, "[-] getpagesize() error: %s\n", strerror(errno));
    return -1;
  } else {
    fprintf(stderr, "[+] getpagesize() = %d-bytes\n", page_size);
    return 0;
  }
}

/**
 * Tests using 'sysconf()' to get pagesize rather than 'getpagesize()'
 */
int test_sysconf_pagesize(void) {
  long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    fprintf(stderr, "[-] sysconf(_SC_PAGESIZE) error: %s\n", strerror(errno));
    return -1;
  } else {
    fprintf(stderr, "[+] sysconf(_SC_PAGESIZE) = %ld-bytes\n", page_size);
    return 0;
  }
}

/**
 * A function that allocates memory directly from the operating system,
 * so it can be used instead of 'malloc()', but would be extremely
 * inefficient to do so. Must be freed with 'mmap_free()'.
 */
void *mmap_allocate(size_t size) {
  return mmap(
      NULL,                         /* We don't care what address we get back */
      size, PROT_READ | PROT_WRITE, /* read/writeable, of course */
      MAP_PRIVATE | MAP_ANONYMOUS,  /* "ANONYMOUS" means not a mapped file */
      -1, /* not a memory mapped file, so fd must be -1 */
      0   /* not a memory mapped file, so offset must be zero */
  );
}

/**
 * An equivelent to the 'free()' function freeing somethign allocated by
 * 'mmap_allocate()'. The major difference is that we need to also include
 * the size of the original allocation
 */
void mmap_free(void *p, size_t size) { munmap(p, size); }

/**
 * Tests using mmap() to allocate/free memory like malloc()/free(). You don't
 * want to do this in general, but this is a starting point for understanding
 * many things you might want to use mmap() for.
 */
int test_mmap_allocate() {
  static const size_t sizeof_foo = 100;
  char *foo;
  unsigned long long t0, t1, t2, t3, t4;

  /* Create the mapping */
  t0 = util_clockcycle();
  foo = mmap_allocate(sizeof_foo);
  if (foo == MAP_FAILED) {
    fprintf(stderr, "[-] mmap(MAP_ANON) failed: %s\n", strerror(errno));
    return -1;
  }

  /* Use the memory */
  t1 = util_clockcycle();
  memcpy(foo, "hello\n", 6);

  /* Free the memory */
  t2 = util_clockcycle();
  mmap_free(foo, sizeof_foo);

  /* Print the results */
  t3 = util_clockcycle();
  t4 = util_clockcycle();
  fprintf(stderr, "[+] times = mmap=%llu use=%llu free=%llu min=%llu\n", t1 - t0, t2 - t1, t3 - t2, t4-t3);
  fprintf(stderr, "[+] mmap(MAP_ANON) succeeded: 0x%lx\n", (long)foo);
  return 0;
}

int main(void) {
  test_getpagesize();
  test_sysconf_pagesize();
  test_mmap_allocate();

  return 0;
}

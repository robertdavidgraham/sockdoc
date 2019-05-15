/*
    This tests "secure" memory by causing a core dump. You should the counter
    increment until it hits the guard page boundary, which will usually be
    4096 minus 64, or 4032. Then you can search memory to make sure it doesn't
    contain the secret.

    macOS:
        First, do 'ulimit -c unlimited' in order to enable crash dumps.
        Second, run this program.
        Third. look in the /cores directory for the dump.
        Fourth, run "strings - /cores/xxx | grep NotSecure5678" to verify
        you do find the insecure memory.
        Fifith, run "strings - /cores/xxx | grep MyPassword1234" to verify
        you don't find the insecure memory.
*/
#include "util-secmem.h"
#include <stdlib.h>
#include <string.h>

int main(void) {
  char *p1;
  char *p2;
  unsigned count = 0;
  size_t i;

  /* Allocate some secure memory */
  p1 = util_secmem_alloc(100);
  p2 = malloc(100);

  /* Copy over a distinctive password into the memory */
  memcpy(p1 + 0, "MyPassw", 7);
  memcpy(p2 + 7, "ord1234", 7);
  memcpy(p2 + 0, "NotSecu", 7);
  memcpy(p2 + 7, "re56789", 7);

  /* Now keep reading the page until we hit a guard page and crash */
  for (i = 0; i < 1000000; i++) {
    fprintf(stderr, "%8d\b\b\b\b\b\b\b\b", (int)i);
    count += p1[i] + p2[i % 14];
  }

  /* At this point, the program should crash and produce a core
   * dump. We can then run 'strings' on the core dump and verify
   * that the non-secure password exists, but the secure one doesn't.
   */
  return count;
}

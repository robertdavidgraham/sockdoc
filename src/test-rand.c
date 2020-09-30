#include "util-rand.h"
#include <time.h>
#include <stdio.h>





int main(int argc, char *argv[])
{
    unsigned counts[13] = {0};
    size_t i;
    util_rand_t ctx[1] = {0};
    time_t now = time(0);

    util_rand_seed(ctx, &now, sizeof(now));

    fprintf(stderr, "[ ] calculating numbers\n");
    for (i=0; i<100000000; i++) {
        uint32_t r;
        r  = util_rand32_uniform(ctx, 13);
        counts[r]++;
    }
    for (i=0; i<13; i++) {
        printf("[%2u] %u\n", (unsigned)i, counts[i]);
    }

}
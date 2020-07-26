#ifndef UTIL_WORKERS_H
#define UTIL_WORKERS_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>

/**
  * Create a 'workers' subsystem, with the desired maximum number of children.
  * This number may be reduced, if the system limits are smaller than desired.
 */
struct workers_t *
workers_init(unsigned *max_children);

/**
  * Spawn a child program with the given argument list.
 */
int
workers_spawn(struct workers_t *workers, const char *progname, size_t argc, char **argv);

int
workers_read(struct workers_t *t, unsigned milliseconds,
             void (*write_stdout)(void *buf, size_t length, void *data),
             void (*write_stderr)(void *buf, size_t length, void *data),
             void *userdata
             );

int
workers_reap(struct workers_t *workers);

void
workers_cleanup(struct workers_t *workers);

size_t
workers_count(const struct workers_t *workers);



#ifdef __cplusplus
}
#endif
#endif

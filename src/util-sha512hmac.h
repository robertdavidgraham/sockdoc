/*
    "SHA-512 keyed Hash-based Message Authentication Code"
    License: public domain

    An HMAC is a keyed hash function, where:
    HMAC(key, message) = HASH(outer(key) + HASH(inner(key) + message))
 */
#ifndef UTIL_SHA512HMAC_H
#define UTIL_SHA512HMAC_H
#include "util-sha512.h"

/**
 * This function holds the 'state' or 'context'. To hash data, this context
 * is first initialized, then multiple updates are done with sequential
 * chunks of data, then at end the 'finalize()' function is called.
 */
typedef struct {
    util_sha512_t sha512ctx;
    unsigned char key[64];
} util_sha512hmac_t;

/**
 * Prepare the state variable with initial values to start
 * hashing. This must be called before the update() functions.
 * @param ctx
 *      This is an out-only parameter whose contents will be
 *      wiped out and replaced with an initial state.
 * @param key
 *      The key for this keyed hash.
 * @param key_length
 *      The size, in bytes, of the key, the size in memory
 *      (rather than the size in bits).
 */
void util_sha512hmac_init(
    util_sha512hmac_t *ctx, const void *key, size_t key_length);

/**
 * Process the next chunk of data to the hash. You can call this
 * function repeatedly for sequential data. This updates the `ctx`
 * state. There are no particular alignment requires, you can call
 * this function a byte at a time or a large chunk at a time.
 * @param ctx
 *      State that was initialied with `init()` before
 *      this function was first called, and which will be finalized
 *      by a call to `final()` after the last call to
 *      this function.
 * @param buf
 *      The next chunk of data to process.
 * @param length
 *      The length of the buffer pointed to by `buf`.
 */
void util_sha512mac_update(
    util_sha512hmac_t *ctx, const void *buf, size_t length);

/**
 * Finalize the hash. This does some ending calculations, such as
 * doing any necessary padding and processing the length. Then,
 * it extracts the hash value and returns the reuslt.
 */
void util_sha512hmac_final(
    util_sha512hmac_t *ctx, unsigned char *digest, size_t digest_length);

/**
 * Calculate a SHA512 of a single chunk of data. this calls the
 * previous three functions in order (init(), update(), final()).
 */
void util_sha512mac(const void *key, size_t key_length, const void *buf,
    size_t length, unsigned char *digest, size_t digest_length);

/**
 * Does a unit-test on this module, calculating various test vectors
 * to verify they come up with the correct results.
 * @return
 *      1 if self-test passes, 0 if self-test fails.
 */
int util_sha512hmac_selftest(void);

#endif

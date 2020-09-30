/*
    "Crypto implementing the SHA-512 hash algorithm"

    Copyright: 2019 by Robert David Graham
    Authors: Robert David Graham
    License: MIT
        https://github.com/robertdavidgraham/sockdoc/blob/master/src/LICENSE
    Dependencies: none
    Standard: FIPS 180-3
    http://csrc.nist.gov/publications/fips/fips180-3/fips180-3_final.pdf
    Standard: RFC 6234
    https://tools.ietf.org/html/rfc6234

 WARNING:
    This is for demonstration purposes only. Crypto libraries such
    as OpenSSL or libsodium should be used instead. They are
    roughly 30 times faster (according to my benchmarks) and address
    subtle security weaknesses like side channel attacks.
 */
#ifndef UTIL_SHA512_H
#define UTIL_SHA512_H
#include <stdint.h>
#include <stdio.h>

/**
 * This structure holds the 'state' or 'context'. To hash data, this context
 * is first initialized, then multiple updates are done with sequential
 * chunks of data, then at end the 'finalize()' function is called.
 */
typedef struct util_sha512_t {
    uint8_t buf[128];
    uint64_t state[8];
    uint64_t length;
    size_t partial;
} util_sha512_t;

/**
 * Prepare the state variable with initial values to start
 * hashing. This must be called before the update() functions.
 * @param ctx
 *      This is an out-only parameter whose contents will be
 *      wiped out and replaced with an initial state.
 */
void util_sha512_init(util_sha512_t *ctx);

/**
 * Process the next chunk of data to the hash. You can call this
 * function repeatedly for sequential data. This updates the `ctx`
 * state. There are no particular alignment requires, you can call
 * this function a byte at a time or a large chunk at a time.
 * @param ctx
 *      State that was initialied with `util_sha512_init()` before
 *      this function was first called, and which will be finalized
 *      by a call to `util_sha512_final()` after the last call to
 *      this function.
 * @param buf
 *      The next chunk of data to process.
 * @param length
 *      The length of the buffer pointed to by `buf`.
 */
void util_sha512_update(util_sha512_t *ctx, const void *buf, size_t length);

/**
 * Finalize the hash. This does some ending calculations, such as
 * doing any necessary padding and processing the length. Then,
 * it extracts the hash value and returns the result.
 * @param ctx
 *      Context/state that was initialied with the 'init()' function,
 *      then used with one or more calls to the 'update()' function
 *      in order to iteratively process data.
 * @param digest
 *      The [out] result of this function.
 * @param digest_length
 *      The length, in bytes, of the output digest. This length
 *      may be less than 64-bytes (meaning 512-bits) when using
 *      truncated ouput, such as in the SHA-512/256 construction
 *      where this parameter will be only 32-bytes (meaning, the
 *      left-most 256-bits will be extracted).
 */
void util_sha512_final(
    util_sha512_t *ctx, unsigned char *digest, size_t digest_length);

/**
 * Calculate a SHA512 of a single chunk of data. this calls the
 * previous three functions in order (init(), update(), final()).
 */
void util_sha512(const void *buf, size_t length, unsigned char *digest,
    size_t digest_length);

/**
 * A quick sanity check of this module. This is not a comprehensive unit
 * test, but verifies basic functionality with a small/quick test that
 * won't bloat the size of executables, and can be called at startup
 * by users of this program without delaying the program.
 * @return 
 *    1 on success, 0 on failure
 */
int util_sha512_selftest(void);

#endif

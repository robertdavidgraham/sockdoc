#include "util-sha512hmac.h"
#include <string.h>

/* For securely wiping memory, prevents compilers from removing this
 * function due to optimizations. */
typedef void *(*memset_t)(void *, int, size_t);
static volatile memset_t secure_memset = memset;

/*
 * This starts that hashing process with the inner-key.
 */
void
util_sha512hmac_init(util_sha512hmac_t *ctx, const void *key, size_t key_length)
{
    unsigned char key_ipad[64];
    size_t i;

    /* Wipe out anything that may be here */
    memset(ctx, 0, sizeof(*ctx));

    /* First, process the key. If it's shorter than our blocksize, it must
     * be padded with zeroes. If it's longer than our blocksize, we'll
     * just hash it down to our size */
    if (key_length <= 64)
        memcpy(ctx->key, key, key_length);
    else
        util_sha512(key, key_length, ctx->key, sizeof(ctx->key));

    /* Calculate the 'inner-pad' version of the key */
    for (i = 0; i < sizeof(key_ipad); i++)
        key_ipad[i] = ctx->key[i] ^ 0x36;

    /* Initialize a SHA-512 context for use with hashing */
    util_sha512_init(&ctx->sha512ctx);

    /* Start the hashing with the inner-pad key. In other words
     * we are calculating SHA512(ipad + message) */
    util_sha512_update(&ctx->sha512ctx, key_ipad, sizeof(key_ipad));
}

/*
 * This update function simply wraps the inner hash function.
 * All the interesting things for HMAC are done either in
 * the 'init()' function or 'final()' function.
 */
void
util_sha512hmac_update(util_sha512hmac_t *ctx, const void *buf, size_t length)
{
    util_sha512_update(&ctx->sha512ctx, buf, length);
}

void
util_sha512hmac_final(
    util_sha512hmac_t *ctx, unsigned char *digest, size_t digest_length)
{
    unsigned char key_opad[64];
    unsigned char idigest[64];
    size_t i;
    util_sha512_t octx;

    /* Finalize the inner-digest of SHA512(ipad + message) */
    util_sha512_final(&ctx->sha512ctx, idigest, sizeof(idigest));

    /* Create the outer-pad version fo the key */
    for (i = 0; i < 64; i++)
        key_opad[i] = ctx->key[i] ^ 0x5c;

    /* Calculate the outer digest */
    util_sha512_init(&octx);
    util_sha512_update(&octx, key_opad, sizeof(key_opad));
    util_sha512_update(&octx, idigest, sizeof(idigest));
    util_sha512_final(&octx, digest, digest_length);

    /* Securely wipe the memory used */
    secure_memset(&octx, 0, sizeof(octx));
    secure_memset(ctx, 0, sizeof(*ctx));
}

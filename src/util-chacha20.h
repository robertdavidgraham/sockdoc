/*
 "Crypto implementing the ChaCha20 encryption algorithm"

 Copyright: 2019 by Robert David Graham
 Authors: DJB, Robert David Graham
 License: MIT
       https://github.com/robertdavidgraham/sockdoc/blob/master/src/LICENSE
 Dependencies: none

 This ia a cryptographic algorithm for encryption/decryption using
 the ChaCha20 stream cipher.

WARNING:
 This code is only for demonstration purposes. You should instead
 use a cryptographic library like OpenSSL or libsodium, which will
 be several times faster and more secure.

WARNING:
 This is not compatible with RFC 7539. It uses only a 64-bit nonce
 instead of a 96-bit nonce.

*/
#ifndef UTIL_CHACHA20_H
#define UTIL_CHACHA20_H
#include <stdint.h>
#include <stdio.h>

/**
 * This structure holds the state of encrypting/decrypting a stream of data.
 * The 'init()' function is called to set this up with a 'key' and 'nonce',
 * after which we sequentially call 'encrypt()' or 'decrypt()' functions.
 * This is what we often call in crypto APIs a "context".
 */
typedef struct {
    uint32_t state[16];
    size_t partial;
} util_chacha20_t;

/**
 * Initialize a ChaCha20 context for either decrypting or encrypting.
 * @param ctx
 *      This stores the state of the encryption/decryption stream.
 *      The contents will be completely overwritten, there is no
 *      need to set these values before calling this initialization
 *      function.
 * @param key
 *      This 256-bit number is the secret key, known only to the sender
 *      and recipient of the encrypted message.
 * @param nonce
 *      This 64-bit number must be unique for each message sent, or each
 *      file encrypted, or each network stream. It can be public (the
 *      attackers can see it), and it doesn't have to be random
 *      (it can be a 64-bit sequential counter). If you are using a
 *      64-bit integer as the nonce, you can call the function
 *      util_chacha20_nonce2bytes() to convert it into the array
 *      of bytes used here.
 */
int util_chacha20_init(util_chacha20_t *ctx, const unsigned char key[32],
    size_t key_length, const unsigned char nonce[8], size_t nonce_length);

/**
 * Encrypts the next chunk of data in the stream. Sequential calls to this
 * function increment the 'ctx' state. The 'plaintext' and 'ciphertext' can
 * point to the same memory.
 *
 * @param plaintext
 *      The original message that will be encrypted.
 * @param ciphertext
 *      The result of the encryption, containing the encrypted data.
 * @param length
 *      The length, in bytes, of input plaintext and the output ciphertext.
 */
void util_chacha20_encrypt(util_chacha20_t *ctx, const void *plaintext,
    void *ciphertext, size_t length);

/**
 * Decrypts the next chunk of data in the stream. Sequential calls to this
 * function increment the 'ctx' state. The 'plaintext' and 'ciphertext' can
 * point to the same memory.
 *
 * @param ciphertext
 *      The encrypted data that we need to decrypt.
 * @param plaintext
 *      The result of decryption, which will contain the original message.
 * @param length
 *      The length, in bytes, of input plaintext and the output ciphertext.
 */
void util_chacha20_decrypt(util_chacha20_t *ctx, const void *ciphertext,
    void *plaintext, size_t length);

/**
 * Do a stateless encryption/decryption somewhere in the middle of a stream.
 * One use for this function is randomly accessing an encrypted file.
 * This function is used for both encryption and decryption. This is because
 * ChaCha20 is a stream-cipher, where encryption and decryption are the same
 * thing, XORing against a keystream.
 *
 * @param key
 *      The 256-bit encryption key.
 * @param nonce
 *      The 64-bit nonce.
 * @param offset
 *      The offset, in bytes, where we are within the encryption stream.
 *      Note that this is not the block counter, but the byte counter.
 *      This means instead of handling data 2^70 in size, it can only
 *      handle objects of 2^64 in size.
 * @param length
 *      The length of this chunk, in bytes. In other words, the length
 *      of the buffers pointed to by 'input' and 'output'.
 * @param input
 *      If this function is being used to encrypt, then this
 *      will point to the plaintext. If decrypting, then this will
 *      point to the ciphertext.
 * @param output
 *      If encrypting, this will point to the ciphertext resulting
 *      from the function. If decrypting, this will point to the
 *      plaintext produced by this function.
 */
void util_chacha20_crypt(const unsigned char key[32],
    const unsigned char nonce[8], uint64_t offset, size_t length,
    const void *input, void *output);

/**
 * This is a simple utility function for the programmer who might be
 * using a 64-bit integer as the nonce, but needs to convert it to
 * an array-of-bytes for input into the 'init()' function above.
 * This simply does a little-endian conversion.
 */
void util_chacha20_nonce2bytes(uint64_t number, unsigned char bytes[8]);

#endif

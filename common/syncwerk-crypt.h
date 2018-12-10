/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
  Description:
  
  The function pair "syncwerk_encrypt/syncwerk_decrypt" are used to
  encrypt/decrypt data in the syncwerk system, using AES 128 bit ecb
  algorithm provided by openssl.
*/  

#ifndef _SYNCWERK_CRYPT_H
#define _SYNCWERK_CRYPT_H

#include <openssl/aes.h>
#include <openssl/evp.h>


/* Block size, in bytes. For AES it can only be 16 bytes. */
#define BLK_SIZE 16
#define ENCRYPT_BLK_SIZE BLK_SIZE

struct SyncwerkCrypt {
    int version;
    unsigned char key[32];   /* set when enc_version >= 1 */
    unsigned char iv[16];
};

typedef struct SyncwerkCrypt SyncwerkCrypt;

SyncwerkCrypt *
syncwerk_crypt_new (int version, unsigned char *key, unsigned char *iv);

/*
  Derive key and iv used by AES encryption from @data_in.
  key and iv is 16 bytes for version 1, and 32 bytes for version 2.

  @data_out: pointer to the output of the encrpyted/decrypted data,
  whose content must be freed by g_free when not used.

  @out_len: pointer to length of output, in bytes

  @data_in: address of input buffer

  @in_len: length of data to be encrpyted/decrypted, in bytes 

  @crypt: container of crypto info.
  
  RETURN VALUES:

  On success, 0 is returned, and the encrpyted/decrypted data is in
  *data_out, with out_len set to its length. On failure, -1 is returned
  and *data_out is set to NULL, with out_len set to -1;
*/

int
syncwerk_derive_key (const char *data_in, int in_len, int version,
                    unsigned char *key, unsigned char *iv);

/*
 * Generate the real key used to encrypt data.
 * The key 32 bytes long and encrpted with @passwd.
 */
void
syncwerk_generate_random_key (const char *passwd, char *random_key);

void
syncwerk_generate_magic (int version, const char *repo_id,
                        const char *passwd, char *magic);

int
syncwerk_verify_repo_passwd (const char *repo_id,
                            const char *passwd,
                            const char *magic,
                            int version);

int
syncwerk_decrypt_repo_enc_key (int enc_version,
                               const char *passwd, const char *random_key,
                               unsigned char *key_out, unsigned char *iv_out);

int
syncwerk_update_random_key (const char *old_passwd, const char *old_random_key,
                           const char *new_passwd, char *new_random_key);

int
syncwerk_encrypt (char **data_out,
                 int *out_len,
                 const char *data_in,
                 const int in_len,
                 SyncwerkCrypt *crypt);


int
syncwerk_decrypt (char **data_out,
                 int *out_len,
                 const char *data_in,
                 const int in_len,
                 SyncwerkCrypt *crypt);

int
syncwerk_decrypt_init (EVP_CIPHER_CTX **ctx,
                      int version,
                      const unsigned char *key,
                      const unsigned char *iv);

#endif  /* _SYNCWERK_CRYPT_H */

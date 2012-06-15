#include "aikido.h"

#include "aikidointerpret.h"

// everything in the aikido namespace
namespace aikido {

// since we are linking these through the dynamic linker we need them
// unmangled
extern "C" {

/*
Dervied from code online:

  AES encryption/decryption demo program using OpenSSL EVP apis
  gcc -Wall openssl_aes.c -lcrypto

  this is public domain code. 

  Saju Pillai (saju.pillai@gmail.com)

*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/md5.h>

/*
 * Encrypt *len bytes of data
 * All data going in & out is considered binary (unsigned char[])
 */
static unsigned char *aes_encrypt(EVP_CIPHER_CTX *e, unsigned char *plaintext, int *len)
{
  /* max ciphertext len for a n bytes of plaintext is n + AES_BLOCK_SIZE -1 bytes */
  int c_len = *len + AES_BLOCK_SIZE, f_len = 0;
  unsigned char *ciphertext = (unsigned char *)malloc(c_len);

  /* allows reusing of 'e' for multiple encryption cycles */
  EVP_EncryptInit_ex(e, NULL, NULL, NULL, NULL);

  /* update ciphertext, c_len is filled with the length of ciphertext generated,
    *len is the size of plaintext in bytes */
  EVP_EncryptUpdate(e, ciphertext, &c_len, plaintext, *len);

  /* update ciphertext with the final remaining bytes */
  EVP_EncryptFinal_ex(e, ciphertext+c_len, &f_len);

  *len = c_len + f_len;
  return ciphertext;
}

/*
 * Decrypt *len bytes of ciphertext
 */
static unsigned char *aes_decrypt(EVP_CIPHER_CTX *e, unsigned char *ciphertext, int *len)
{
  /* plaintext will always be equal to or lesser than length of ciphertext*/
  int p_len = *len, f_len = 0;
  unsigned char *plaintext = (unsigned char *)malloc(p_len);
  
  EVP_DecryptInit_ex(e, NULL, NULL, NULL, NULL);
  EVP_DecryptUpdate(e, plaintext, &p_len, ciphertext, *len);
  EVP_DecryptFinal_ex(e, plaintext+p_len, &f_len);

  *len = p_len + f_len;
  return plaintext;
}



AIKIDO_NATIVE (internal_aes_makekey) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "aes_makekey", "Invalid password");
    }
    if (paras[2].type != T_INTEGER) {
        throw newParameterException (vm, stack, "aes_makekey", "Invalid keylen type");
    }

    if (paras[3].type != T_VECTOR) {
        throw newParameterException (vm, stack, "aes_makekey", "Invalid salt (need vector");
    }
    if (paras[3].vec->size() != 8) {
        throw newParameterException (vm, stack, "aes_makekey", "Invalid salt length (need 8 bytes)");
    }

    unsigned char *password = (unsigned char *)paras[1].str->c_str();
    int passlen = paras[1].str->size();
    Value::vector *saltvec = paras[2].vec;

    unsigned int keylen = paras[2].integer;
    if (keylen != 16 && keylen != 32) {
        throw newParameterException (vm, stack, "aes_makekey", "Invalid key length (must be 16 or 32)");
    }

    unsigned char salt[8];
    for (int i = 0 ; i < 8 ; i++) {
        salt[i] = (unsigned char)((*saltvec)[i].integer & 0xff);
    }

    const int nrounds = 5;
    unsigned char key[32], iv[32];
  
    int i = EVP_BytesToKey(keylen == 32 ? EVP_aes_256_cbc() : EVP_aes_128_cbc(), EVP_sha1(), salt, password, passlen, nrounds, key, iv);
    if (i != 32) {
       throw newException (vm, stack, "Invalid generated key length");
    }

    Value::vector *result = new Value::vector();

    Value::vector *keyvec = new Value::vector();
    for (int i = 0 ; i < keylen ; i++) {
        keyvec->push_back (key[i]);
    }
    result->push_back (Value(keyvec));

    Value::vector *ivvec = new Value::vector();
    for (int i = 0 ; i < 16 ; i++) {
        ivvec->push_back (iv[i]);
    }
    result->push_back (Value(ivvec));
    return result;
}

AIKIDO_NATIVE (internal_aes_encrypt) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "aes_encrypt", "Invalid value");
    }
    if (paras[2].type != T_VECTOR) {
        throw newParameterException (vm, stack, "aes_encrypt", "Invalid key (need vector)");
    }

    if (paras[3].type != T_VECTOR) {
        throw newParameterException (vm, stack, "aes_encrypt", "Invalid IV (need vector)");
    }

    std::string v = paras[1].str->str;
    Value::vector *keyvec = paras[2].vec;
    unsigned int keylen = keyvec->size();
    if (keylen != 16 && keylen != 32) {
        throw newParameterException (vm, stack, "aes_encrypt", "Invalid key length (need 16 or 32)");
    }

    Value::vector *ivvec = paras[3].vec;
    if (ivvec->size() != 16) {
        throw newParameterException (vm, stack, "aes_encrypt", "Invalid IV length (need 16)");
    }


    unsigned char key[32];
    for (int i = 0 ; i < keylen ; i++) {
        key[i] = (*keyvec)[i].integer & 0xff;
    }

    unsigned char iv[16];		// initialization vector
    for (int i = 0 ; i < 16 ; i++) {
        iv[i] = (*ivvec)[i].integer & 0xff;
    }

    Value::vector *result = new Value::vector();

    EVP_CIPHER_CTX ctx;
    EVP_CIPHER_CTX_init(&ctx);
    EVP_EncryptInit_ex(&ctx, keylen == 32 ? EVP_aes_256_cbc() : EVP_aes_128_cbc(), NULL, key, iv);

    int len = (int)v.size();

    unsigned char *encoded = aes_encrypt (&ctx, (unsigned char *)v.c_str(), &len);
    memset (key, 0, sizeof(key));		// get rid of key from memory

    for (int i = 0 ; i < len ; i++) {
        result->push_back (Value (encoded[i]));
    }
    free (encoded);

    return result;
}


AIKIDO_NATIVE (internal_aes_decrypt) {
    if (paras[1].type != T_VECTOR && paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "aes_decrypt", "Invalid value");
    }
    if (paras[2].type != T_VECTOR) {
        throw newParameterException (vm, stack, "aes_decrypt", "Invalid key (need vector)");
    }

    if (paras[3].type != T_VECTOR) {
        throw newParameterException (vm, stack, "aes_encrypt", "Invalid IV (need vector)");
    }


    Value::vector *v;
    bool tidyvec = false;
    if (paras[1].type == T_VECTOR) {
       v = paras[1].vec;
    } else {
       tidyvec = true;
       v = new Value::vector();
       std::string s = paras[1].str->str;
       for (unsigned int i = 0 ; i < s.size() ; i++) {
           v->push_back (Value (s[i]));
       }
    }

    unsigned char *ciphertext = (unsigned char *)malloc (v->size());
    for (int i = 0 ; i < v->size() ; i++) {
        ciphertext[i] = (*v)[i].integer & 0xff;
    }

    Value::vector *keyvec = paras[2].vec;
    unsigned int keylen = keyvec->size();
    if (keylen != 16 && keylen != 32) {
        throw newParameterException (vm, stack, "aes_decrypt", "Invalid key length (need 16 or 32)");
    }

    Value::vector *ivvec = paras[3].vec;
    if (ivvec->size() != 16) {
        throw newParameterException (vm, stack, "aes_decrypt", "Invalid IV length (need 16)");
    }


    unsigned char key[32];
    for (int i = 0 ; i < keylen ; i++) {
        key[i] = (*keyvec)[i].integer & 0xff;
    }

    unsigned char iv[16];		// initialization vector
    for (int i = 0 ; i < 16 ; i++) {
        iv[i] = (*ivvec)[i].integer & 0xff;
    }

    EVP_CIPHER_CTX ctx;
    EVP_CIPHER_CTX_init(&ctx);
    EVP_DecryptInit_ex(&ctx, keylen == 32 ? EVP_aes_256_cbc() : EVP_aes_128_cbc(), NULL, key, iv);

    int len = (int)v->size();

    std::string result;
    unsigned char *cleartext = aes_decrypt (&ctx, ciphertext, &len);
    memset (key, 0, sizeof(key));		// get rid of key from memory
    for (int i = 0 ; i < len ; i++) {
        result += cleartext[i];
    }
    free (cleartext);
    free (ciphertext);

    if (tidyvec) {
        delete v;
    }
    return new string(result);
}

AIKIDO_NATIVE(internal_sha1) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "sha1", "Invalid string");
    }

    std::string in = paras[1].str->str;

    unsigned char md[SHA_DIGEST_LENGTH];

    unsigned char *digest = SHA1((const unsigned char*)in.c_str(), in.size(), md);
    char buf[16];
    std::string result;
    for (int i = 0 ; i < SHA_DIGEST_LENGTH ; i++) {
       snprintf (buf, sizeof(buf), "%02x", digest[i]);
       result += buf;
    }
    return new string (result);
}

AIKIDO_NATIVE(internal_md5) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "md5", "Invalid string");
    }

    std::string in = paras[1].str->str;

    unsigned char md[MD5_DIGEST_LENGTH];

    unsigned char *digest = MD5((const unsigned char*)in.c_str(), in.size(), md);
    char buf[16];
    std::string result;
    for (int i = 0 ; i < MD5_DIGEST_LENGTH ; i++) {
       snprintf (buf, sizeof(buf), "%02x", digest[i]);
       result += buf;
    }
    return new string (result);
}


}

}


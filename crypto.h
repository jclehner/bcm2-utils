#ifndef BCM2UTILS_CRYPTO_H
#define BCM2UTILS_CRYPTO_H
#ifndef __APPLE__
#include <openssl/aes.h>
#include <openssl/md5.h>
#else
#include <CommonCrypto/CommonCrypto.h>
#include <CommonCrypto/CommonDigest.h>

typedef CC_MD5_CTX MD5_CTX;

inline int MD5_Init(MD5_CTX* ctx)
{ return CC_MD5_Init(ctx); }

inline int MD5_Update(MD5_CTX* ctx, const void* data, unsigned long len)
{ return CC_MD5_Update(ctx, data, len); }

inline int MD5_Final(unsigned char* md, MD5_CTX* ctx)
{ return CC_MD5_Final(md, ctx); }

typedef struct
{
	CCCryptorRef cryptor;
} AES_KEY;

int AES_set_encrypt_key(const unsigned char* key, const int bits, AES_KEY* akey);
int AES_set_decrypt_key(const unsigned char* key, const int bits, AES_KEY* akey);
int AES_encrypt(const unsigned char* in, unsigned char* out, AES_KEY* akey);
int AES_decrypt(const unsigned char* in, unsigned char* out, AES_KEY* akey);
#endif


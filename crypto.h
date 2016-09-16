#ifndef BCM2UTILS_CRYPTO_H
#define BCM2UTILS_CRYPTO_H
#if !defined(__APPLE__) || !defined(BCM2UTILS_USE_COMMON_CRYPTO)
#include <openssl/aes.h>
#include <openssl/md5.h>
#else
#include <CommonCrypto/CommonCrypto.h>
#include <CommonCrypto/CommonDigest.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef CC_MD5_CTX MD5_CTX;

inline int MD5_Init(MD5_CTX* ctx)
{ return CC_MD5_Init(ctx); }

inline int MD5_Update(MD5_CTX* ctx, const void* data, unsigned long len)
{ return CC_MD5_Update(ctx, data, len); }

inline int MD5_Final(unsigned char* md, MD5_CTX* ctx)
{ return CC_MD5_Final(md, ctx); }

#ifdef __cplusplus
}
#endif

#endif
#endif


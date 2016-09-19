#include <stdexcept>
#include <cstring>
#include "crypto.h"

#if defined(__APPLE__)
#include <CommonCrypto/CommonCrypto.h>
#include <CommonCrypto/CommonDigest.h>

#ifndef MD5_CTX
#define MD5_CTX CC_MD5_CTX;
#define MD5_Init(ctx) CC_MD5_Init(ctx)
#define MD5_Update(ctx, data, len) CC_MD5_Update(ctx, data, len)
#define MD5_Final(md, ctx) CC_MD5_Final(md, ctx)
#endif

#define BCM2UTILS_USE_COMMON_CRYPTO

#elif defined(_WIN32)
#error "Platform not yet supported"
#include <windows.h>
#include <wincrypt.h>
#define BCM2UTILS_USE_WINCRYPT
#else
#include <openssl/aes.h>
#include <openssl/md5.h>
#define BCM2UTILS_USE_OPENSSL
#endif

using namespace std;

namespace bcm2utils {
namespace {
inline const unsigned char* data(const string& buf)
{
	return reinterpret_cast<const unsigned char*>(buf.data());
}
}

string hash_md5_keyed(const string& buf, const string& key)
{
#ifndef BCM2UTILS_USE_WINCRYPT
	MD5_CTX ctx;

	MD5_Init(&ctx);
	MD5_Update(&ctx, data(buf), buf.size());
	if (!key.empty()) {
		MD5_Update(&ctx, data(key), key.size());
	}

	string md5(16, '\0');
	MD5_Final(reinterpret_cast<unsigned char*>(&md5[0]), &ctx);
	return md5;
#endif
}

string crypt_aes_256_ecb(const string& ibuf, const string& key, bool encrypt)
{
#if defined(BCM2UTILS_USE_OPENSSL)
	AES_KEY aes;
	int err;

	if (!encrypt) {
		err = AES_set_decrypt_key(data(key), 256, &aes);
	} else {
		err = AES_set_encrypt_key(data(key), 256, &aes);
	}

	if (err) {
		throw runtime_error("AES_set_XXcrypt_key: error " + to_string(err));
	}

	string obuf(ibuf.size(), '\0');

	auto remaining = ibuf.size();
	// this is legal in C++11... ugly, but legal!
	auto iblock = data(ibuf);
	auto oblock = reinterpret_cast<unsigned char*>(&obuf[0]);

	while (remaining >= 16) {
		if (!encrypt) {
			AES_decrypt(iblock, oblock, &aes);
		} else {
			AES_encrypt(iblock, oblock, &aes);
		}

		remaining -= 16;
		iblock += 16;
		oblock += 16;
	}

	if (remaining) {
		memcpy(oblock, iblock, remaining);
	}

	return obuf;
#elif defined(BCM2UTILS_USE_COMMON_CRYPTO)
	size_t len = align_left(ibuf.size(), 16), moved;
	string obuf(len, '\0');

	CCCryptorStatus ret = CCCrypt(
			encrypt ? kCCEncrypt ? kCCDecrypt,
			kCCAlgorithmAES128,
			kCCOptionECBMode,
			key.data(), kCCKeySizeAES256,
			nullptr,
			ibuf.data(), len,
			&obuf[0], len,
			&moved);

	if (ret != kCCSuccess) {
		throw runtime_error("CCCrypt: error " + to_string(ret));
	} else if (moved != len) {
		throw runtime_error("CCCrypt: expected length " + to_string(len) + ", got " + to_string(moved));
	}

	if (len < ibuf.size()) {
		obuf += ibuf.substr(len);
	}

	return obuf;
#endif
}









}


#ifdef __APPLE__
static int set_key(bool encrypt, const unsigned char *key, int bits, AES_KEY* akey)
{
	if (bits != 128 && bits != 256) {
		return -1;
	}

	CCCryptorStatus ret = CCCryptorCreate(
			encrypt ? kCCEncrypt : kCCDecrypt,
			kCCAlgorithmAES128,
			kCCOptionECBMode,
			key,
			bits == 256 ? kCCKeySizeAES256 : kCCKeySizeAES128,
			NULL,
			&akey->cryptor);

	return ret == kCCSuccess ? 0 : -1;
}

static int crypt(const unsigned char* in, unsigned char* out, AES_KEY *akey)
{
	size_t moved;
	CCCryptorStatus ret = CCCryptorUpdate(
			akey->cryptor,
			in, 16, out, 16,
			&moved);
	return ret == kCCSuccess ? 0 : -1;
}

inline int AES_set_encrypt_key(const unsigned char *key, const int bits, AES_KEY* akey)
{ return set_key(true, key, bits, akey); }

inline int AES_set_decrypt_key(const unsigned char *key, const int bits, AES_KEY* akey)
{ return set_key(false, key, bits, akey); }

int AES_encrypt(const unsigned char* in, unsigned char* out, AES_KEY* akey)
{ return crypt(in, out, akey); }

int AES_decrypt(const unsigned char* in, unsigned char* out, AES_KEY* akey)
{ return crypt(in, out, akey); }
#endif

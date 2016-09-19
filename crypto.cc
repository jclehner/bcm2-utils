#include <stdexcept>
#include <cstring>
#include "crypto.h"
#include "util.h"

#if defined(__APPLE__)
#include <CommonCrypto/CommonCrypto.h>
#include <CommonCrypto/CommonDigest.h>

#ifndef MD5_CTX
#define MD5_CTX CC_MD5_CTX
#define MD5_Init(ctx) CC_MD5_Init(ctx)
#define MD5_Update(ctx, data, len) CC_MD5_Update(ctx, data, len)
#define MD5_Final(md, ctx) CC_MD5_Final(md, ctx)
#endif

#define BCM2UTILS_USE_COMMON_CRYPTO

#elif defined(_WIN32)
// this is required for wine
#define USE_WS_PREFIX
#include <windows.h>
#include <wincrypt.h>
#include <system_error>
#define BCM2UTILS_USE_WINCRYPT
#else
#include <openssl/aes.h>
#include <openssl/md5.h>
#define BCM2UTILS_USE_OPENSSL
#endif

using namespace std;
using namespace bcm2dump;

namespace bcm2utils {
namespace {
inline const unsigned char* data(const string& buf)
{
	return reinterpret_cast<const unsigned char*>(buf.data());
}

#ifdef BCM2UTILS_USE_WINCRYPT
class winapi_error : public system_error
{
	public:
	winapi_error(const string& what, DWORD error = GetLastError())
	: system_error(error_code(error, generic_category()), what)
	{}
};

class crypt_context
{
	public:
	crypt_context()
	{
		if (!CryptAcquireContext(&handle, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
			throw winapi_error("CryptAcquireContext");
		}
	}

	~crypt_context()
	{
		CryptReleaseContext(handle, 0);
	}

	HCRYPTPROV handle;
};

#endif
}

string hash_md5(const string& buf)
{
	string md5(16, '\0');
#ifndef BCM2UTILS_USE_WINCRYPT
	MD5_CTX ctx;

	MD5_Init(&ctx);
	MD5_Update(&ctx, data(buf), buf.size());
	MD5_Final(reinterpret_cast<unsigned char*>(&md5[0]), &ctx);

	return md5;
#else
	crypt_context ctx;
	HCRYPTHASH hash;

	if (!CryptCreateHash(ctx.handle, CALG_MD5, 0, 0, &hash)) {
		throw winapi_error("CryptCreateHash");
	}

	auto c = bcm2dump::cleaner([hash] () {
		CryptDestroyHash(hash);
	});

	if (!CryptHashData(hash, data(buf), buf.size(), 0)) {
		throw winapi_error("CryptHashData");
	}

	DWORD size = md5.size();
	if (!CryptGetHashParam(hash, HP_HASHVAL, reinterpret_cast<unsigned char*>(&md5[0]), &size, 0)) {
		throw winapi_error("CryptGetHashParam");
	}

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
	size_t moved;
	size_t len = align_left(ibuf.size(), 16);
	string obuf(len, '\0');

	CCCryptorStatus ret = CCCrypt(
			encrypt ? kCCEncrypt : kCCDecrypt,
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
#else
	throw runtime_error("encryption not supported on this platform");
#endif
}
}

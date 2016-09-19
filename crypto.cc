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

class crypt_context
{
	public:
	crypt_context()
	: handle(0)
	{
		BOOL ok = CryptAcquireContext(
				&handle,
				nullptr,
				MS_ENH_RSA_AES_PROV,
				PROV_RSA_AES,
				CRYPT_VERIFYCONTEXT);

		if (!ok) {
			throw winapi_error("CryptAcquireContext");
		}
	}

	~crypt_context()
	{
		if (handle) {
			CryptReleaseContext(handle, 0);
		}
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
#elif defined(BCM2UTILS_USE_WINCRYPT)
	crypt_context ctx;

	// THIS API IS LUDICROUS!!!

	struct {
		BLOBHEADER hdr;
		const DWORD key_size = sizeof(key);
		BYTE key[32];
	} aes;

	aes.hdr = {
		.bType = PLAINTEXTKEYBLOB,
		.bVersion = CUR_BLOB_VERSION,
		.reserved = 0,
		.aiKeyAlg = CALG_AES_256
	};

	memcpy(aes.key, key.data(), aes.key_size);

	HCRYPTKEY hkey = 0;

	if (!CryptImportKey(ctx.handle, reinterpret_cast<BYTE*>(&aes), sizeof(aes), 0, 0, &hkey)) {
		throw winapi_error("CryptImportKey");
	}

	auto c = bcm2dump::cleaner([hkey] () {
			if (hkey) {
				CryptDestroyKey(hkey);
			}
	});

	DWORD mode = CRYPT_MODE_ECB;
	if (!CryptSetKeyParam(hkey, KP_MODE, reinterpret_cast<BYTE*>(&mode), 0)) {
		throw winapi_error("CryptSetKeyParam");
	}

	DWORD len = align_left(ibuf.size(), 16);
	string obuf = ibuf;

	// since we want to avoid wincrypt's padding, pass FALSE to both
	// crypt functions, even though it's technically the final (and only) block

	BOOL ok;
	if (encrypt) {
		ok = CryptEncrypt(hkey, 0, FALSE, 0, reinterpret_cast<unsigned char*>(&obuf[0]), &len, len);
	} else {
		ok = CryptDecrypt(hkey, 0, FALSE, 0, reinterpret_cast<unsigned char*>(&obuf[0]), &len);
	}

	if (!ok) {
		throw winapi_error(encrypt ? "CryptEncrypt" : "CryptDecrypt");
	}

	return obuf + (len < ibuf.size() ? ibuf.substr(len) : "");
#else
	throw runtime_error("encryption not supported on this platform");
#endif
}
}

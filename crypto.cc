/**
 * bcm2-utils
 * Copyright (C) 2016 Joseph C. Lehner <joseph.c.lehner@gmail.com>
 *
 * bcm2-utils is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * bcm2-utils is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with bcm2-utils.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

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
#include <openssl/des.h>
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

void check_keysize(const string& key, size_t size, const string& name)
{
	if (key.size() != size) {
		throw invalid_argument("invalid key size for algorithm " + name);
	}
}

#ifdef BCM2UTILS_USE_OPENSSL
inline const_DES_cblock* to_ccblock(const uint8_t* buf)
{
	return (const_DES_cblock*)buf;
}

inline DES_cblock* to_cblock(uint8_t* buf)
{
	return (DES_cblock*)buf;
}

inline const_DES_cblock* to_ccblock(const string& buf, size_t offset)
{
	return (const_DES_cblock*)&buf[offset];
}

#endif

#ifdef BCM2UTILS_USE_WINCRYPT
class wincrypt_context
{
	public:
	wincrypt_context()
	: handle(0)
	{
		BOOL ok = CryptAcquireContext(
				&handle,
				nullptr,
#ifndef BCM2CFG_WINXP
				MS_ENH_RSA_AES_PROV,
#else
				MS_ENH_RSA_AES_PROV_XP,
#endif
				PROV_RSA_AES,
				CRYPT_VERIFYCONTEXT);

		if (!ok) {
			throw winapi_error("CryptAcquireContext");
		}
	}

	~wincrypt_context()
	{
		if (handle) {
			CryptReleaseContext(handle, 0);
		}
	}

	HCRYPTPROV handle;
};

class wincrypt_hash
{
	public:
	wincrypt_hash(ALG_ID algo)
	: m_hash(0)
	{
		if (!CryptCreateHash(m_ctx.handle, algo, 0, 0, &m_hash)) {
			throw winapi_error("CryptCreateHash");
		}
	}

	~wincrypt_hash()
	{
		if (m_hash) {
			CryptDestroyHash(m_hash);
		}
	}

	HCRYPTHASH get() const
	{ return m_hash; }

	private:
	wincrypt_context m_ctx;
	HCRYPTHASH m_hash;
};

template<size_t KeySize> class wincrypt_key
{
	public:
	wincrypt_key(ALG_ID algo, const string& key)
	: m_key(0)
	{
		// THIS API IS LUDICROUS!!!

		struct {
			BLOBHEADER hdr;
			const DWORD key_size = KeySize;
			BYTE key[KeySize];
		} blob;

		blob.hdr = {
			.bType = PLAINTEXTKEYBLOB,
			.bVersion = CUR_BLOB_VERSION,
			.reserved = 0,
			.aiKeyAlg = algo
		};

		memcpy(blob.key, key.data(), KeySize);

		if (!CryptImportKey(m_ctx.handle, reinterpret_cast<BYTE*>(&blob), sizeof(blob), 0, 0, &m_key)) {
			throw winapi_error("CryptImportKey");
		}
	}

	~wincrypt_key()
	{
		if (m_key) {
			CryptDestroyKey(m_key);
		}
	}

	HCRYPTKEY get() const
	{ return m_key; }

	private:
	wincrypt_context m_ctx;
	HCRYPTKEY m_key;
};

#endif

#if defined(BCM2UTILS_USE_COMMON_CRYPTO)
string crypt_generic_ecb(CCAlgorithm algo, size_t keysize, size_t blocksize, const string& ibuf, const string& key, bool encrypt)
{
	if (key.size() != keysize) {
		throw invalid_argument("invalid key size for algorithm");
	}

	size_t moved;
	size_t len = align_left(ibuf.size(), blocksize);
	string obuf(len, '\0');

	CCCryptorStatus ret = CCCrypt(
			encrypt ? kCCEncrypt : kCCDecrypt,
			algo,
			kCCOptionECBMode,
			key.data(), keysize,
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
}
#elif defined(BCM2UTILS_USE_WINCRYPT)
template<size_t KeySize> string crypt_generic_ecb(ALG_ID algo, size_t blocksize, const string& ibuf, const string& key, bool encrypt)
{
	wincrypt_key<KeySize> ckey(algo, key);

	DWORD mode = CRYPT_MODE_ECB;
	if (!CryptSetKeyParam(ckey.get(), KP_MODE, reinterpret_cast<BYTE*>(&mode), 0)) {
		throw winapi_error("CryptSetKeyParam");
	}

	DWORD len = align_left(ibuf.size(), blocksize);
	string obuf = ibuf;

	// since we want to avoid wincrypt's padding, pass FALSE to both
	// crypt functions, even though it's technically the final (and only) chunk

	BOOL ok;
	if (encrypt) {
		ok = CryptEncrypt(ckey.get(), 0, FALSE, 0, reinterpret_cast<unsigned char*>(&obuf[0]), &len, len);
	} else {
		ok = CryptDecrypt(ckey.get(), 0, FALSE, 0, reinterpret_cast<unsigned char*>(&obuf[0]), &len);
	}

	if (!ok) {
		throw winapi_error(encrypt ? "CryptEncrypt" : "CryptDecrypt");
	}

	// no need to deal with the remaining data, since we copied ibuf to obuf
	return obuf;
}
#elif defined(BCM2UTILS_USE_OPENSSL)
template<size_t BlockSize, class F> string crypt_generic_ecb(const string& ibuf, const F& crypter)
{
	string obuf = ibuf;

	for (size_t i = 0; (i + BlockSize - 1) < ibuf.size(); i += BlockSize) {
		crypter(data(ibuf) + i, reinterpret_cast<uint8_t*>(&obuf[i]));
	}

	return obuf;
}
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
	wincrypt_hash hash(CALG_MD5);

	if (!CryptHashData(hash.get(), data(buf), buf.size(), 0)) {
		throw winapi_error("CryptHashData");
	}

	DWORD size = md5.size();
	if (!CryptGetHashParam(hash.get(), HP_HASHVAL, reinterpret_cast<unsigned char*>(&md5[0]), &size, 0)) {
		throw winapi_error("CryptGetHashParam");
	}

	return md5;
#endif
}

string crypt_3des_ecb(const string& ibuf, const string& key, bool encrypt)
{
	check_keysize(key, 24, "3des-ecb");

#if defined(BCM2UTILS_USE_OPENSSL)
	DES_key_schedule ks[3];

	for (int i = 0; i < 3; ++i) {
		DES_set_key_unchecked(to_ccblock(key, i * 8), &ks[i]);
	}

	return crypt_generic_ecb<8>(ibuf, [&ks, &encrypt](const uint8_t *iblock, uint8_t *oblock) {
			DES_ecb3_encrypt(to_ccblock(iblock), to_cblock(oblock), &ks[0], &ks[1], &ks[2],
					encrypt ? DES_ENCRYPT : DES_DECRYPT);
	});
#elif defined(BCM2UTILS_USE_COMMON_CRYPTO)
	return crypt_generic_ecb(kCCAlgorithm3DES, kCCKeySize3DES, 8, ibuf, key, encrypt);
#elif defined(BCM2UTILS_USE_WINCRYPT)
	return crypt_generic_ecb<24>(CALG_3DES, 8, ibuf, key, encrypt);
#else
	throw runtime_error("encryption not supported on this platform");
#endif
}

string crypt_des_ecb(const string& ibuf, const string& key, bool encrypt)
{
	check_keysize(key, 8, "des-ecb");

#if defined(BCM2UTILS_USE_OPENSSL)
	DES_key_schedule ks;
	DES_set_key_unchecked(to_ccblock(key, 0), &ks);

	return crypt_generic_ecb<8>(ibuf, [&ks, &encrypt](const uint8_t *iblock, uint8_t *oblock) {
			DES_ecb_encrypt(to_ccblock(iblock), to_cblock(oblock), &ks,
					encrypt ? DES_ENCRYPT : DES_DECRYPT);
	});
#elif defined(BCM2UTILS_USE_COMMON_CRYPTO)
	return crypt_generic_ecb(kCCAlgorithmDES, kCCKeySizeDES, 8, ibuf, key, encrypt);
#elif defined(BCM2UTILS_USE_WINCRYPT)
	return crypt_generic_ecb<8>(CALG_DES, 8, ibuf, key, encrypt);
#else
	throw runtime_error("encryption not supported on this platform");
#endif
}

string crypt_aes_256_ecb(const string& ibuf, const string& key, bool encrypt)
{
	check_keysize(key, 32, "aes-256-ecb");

#if defined(BCM2UTILS_USE_OPENSSL)
	AES_KEY aes;
	int err;

	if (!encrypt) {
		err = AES_set_decrypt_key(data(key), 256, &aes);
	} else {
		err = AES_set_encrypt_key(data(key), 256, &aes);
	}

	if (err) {
		throw runtime_error("failed to set AES key: error " + to_string(err));
	}

	return crypt_generic_ecb<16>(ibuf, [&aes, &encrypt](const uint8_t *iblock, uint8_t *oblock) {
			if (encrypt) {
				AES_encrypt(iblock, oblock, &aes);
			} else {
				AES_decrypt(iblock, oblock, &aes);
			}
	});
#elif defined(BCM2UTILS_USE_COMMON_CRYPTO)
	return crypt_generic_ecb(kCCAlgorithmAES128, kCCKeySizeAES256, 16, ibuf, key, encrypt);
#elif defined(BCM2UTILS_USE_WINCRYPT)
	return crypt_generic_ecb<32>(CALG_AES_256, 16, ibuf, key, encrypt);
#else
	throw runtime_error("encryption not supported on this platform");
#endif
}

namespace {
uint32_t srand_motorola;

int32_t rand_motorola()
{
	uint32_t result, next = srand_motorola;

	next *= 0x41c64e6d;
	next += 0x3039;
	result = next & 0xffe00000;

	next *= 0x41c64e6d;
	next += 0x3039;
	result += (next & 0xfffc0000) >> 11;

	next *= 0x41c64e6d;
	next += 0x3039;
	result = (result + (next >> 25)) & 0x7fffffff;

	srand_motorola = next;
	return result;
}
}

// this is some snakeoily shit right here!
string crypt_motorola(string buf, const string& key)
{
	check_keysize(key, 1, "motorola");

	srand_motorola = key[0] & 0xff;

	for (size_t i = 0; i < buf.size(); ++i) {
		float r = rand_motorola();
		int x = ((r / 0x7fffffff) * 255) + 1;
		buf[i] ^= x;
	}

	return buf;
}

// ditto!
string crypt_sub_16x16(string buf, bool encrypt)
{
	char key[16][16];

	for (int i = 0; i < 16; ++i) {
		for (int k = 0; k < 16; k += 2) {
			key[i][k] = (i * 16) + k;
		}
	}

	for (size_t i = 0; i < (buf.size() / 16) * 16; ++i) {
		int k = key[i / 16][i % 16];

		if (encrypt) {
			buf[i] = (buf[i] - k) & 0xff;
		} else {
			buf[i] = (buf[i] + k) & 0xff;
		}
	}

	return buf;
}

string crypt_xor_char(string buf, const string& key)
{
	check_keysize(key, 1, "xor");

	for (size_t i = 0; i < buf.size(); ++i) {
		buf[i] ^= (key[0] & 0xff);
	}

	return buf;
}
}

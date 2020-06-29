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

inline unsigned char* data(string& buf)
{
	return reinterpret_cast<unsigned char*>(&buf[0]);
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

AES_KEY make_aes_key(const string& key, bool encrypt)
{
	AES_KEY ret;
	int err;

	if (!encrypt) {
		err = AES_set_decrypt_key(data(key), key.size() * 8, &ret);
	} else {
		err = AES_set_encrypt_key(data(key), key.size() * 8, &ret);
	}

	if (err) {
		throw runtime_error("failed to set AES key: error " + to_string(err));
	}

	return ret;
}
#endif

#if defined(BCM2UTILS_USE_WINCRYPT) || defined(BCM2UTILS_USE_COMMON_CRYPTO)
#ifdef BCM2UTILS_USE_WINCRYPT
struct enctype
{
	const ALG_ID algo;
	const size_t keysize;
	const size_t blocksize;
	const bool ecb;
};

const enctype et_aes_256_ecb = { CALG_AES_256, 32, 16, true };
const enctype et_aes_128_cbc = { CALG_AES_128, 16, 16, false };
const enctype et_3des_ecb = { CALG_3DES, 24, 8, true };
const enctype et_des_ecb = { CALG_DES, 8, 8, true };
#else
struct enctype
{
	const CCAlgorithm algo;
	const size_t keysize;
	const size_t blocksize;
	const bool ecb;
};

const enctype et_aes_256_ecb = { kCCAlgorithmAES128, 32, 16, true };
const enctype et_aes_128_cbc = { kCCAlgorithmAES128, 16, 16, false };
const enctype et_3des_ecb = { kCCAlgorithm3DES, 24, 16, true };
const enctype et_des_ecb = { kCCAlgorithmDES, 8, 8, true };
#endif

void check_keysize(const string& key, const enctype& et)
{
	if (key.size() != et.keysize + (et.ecb ? 0 : et.blocksize)) {
		throw invalid_argument("invalid key size for algorithm");
	}
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

class wincrypt_key
{
	public:
	wincrypt_key(const enctype& et, const string& key)
	: m_key(0)
	{
		// THIS API IS LUDICROUS!!!

		struct {
			BLOBHEADER hdr;
			DWORD keysize;
			BYTE key[32]; // max key size
		} blob;

		if (et.keysize > sizeof(blob.key)) {
			throw runtime_error("key size out of range");
		}

		check_keysize(key, et);

		blob.keysize = et.keysize;
		blob.hdr = {
			.bType = PLAINTEXTKEYBLOB,
			.bVersion = CUR_BLOB_VERSION,
			.reserved = 0,
			.aiKeyAlg = et.algo
		};

		memcpy(blob.key, key.data(), et.keysize);

		if (!CryptImportKey(m_ctx.handle, reinterpret_cast<BYTE*>(&blob), sizeof(blob), 0, 0, &m_key)) {
			throw winapi_error("CryptImportKey");
		}

		DWORD mode = et.ecb ? CRYPT_MODE_ECB : CRYPT_MODE_CBC;
		set_param(KP_MODE, &mode);

		if (!et.ecb) {
			set_param(KP_IV, key.data() + et.keysize);
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

	template<class T> void set_param(DWORD param, const T* data)
	{
		if (!CryptSetKeyParam(m_key, param, reinterpret_cast<const BYTE*>(data), 0)) {
			throw winapi_error("CryptSetKeyParam");
		}

	}

	wincrypt_context m_ctx;
	HCRYPTKEY m_key;
};

#endif

#if defined(BCM2UTILS_USE_COMMON_CRYPTO)

string crypt_generic(const enctype& et, const string& ibuf, const string& key, bool encrypt)
{
	check_keysize(key, et);

	size_t moved;
	size_t len = align_left(ibuf.size(), et.blocksize);
	string obuf(len, '\0');

	CCCryptorStatus ret = CCCrypt(
			encrypt ? kCCEncrypt : kCCDecrypt,
			et.algo,
			et.ecb ? kCCOptionECBMode : 0,
			key.data(), et.keysize,
			et.ecb ? nullptr : key.data() + et.keysize,
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
string crypt_generic(const enctype& et, const string& ibuf, const string& key, bool encrypt)
{
	wincrypt_key ckey(et, key);

	DWORD len = align_left(ibuf.size(), et.blocksize);
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
#if defined(BCM2UTILS_USE_OPENSSL)
	check_keysize(key, 24, "3des-ecb");
	DES_key_schedule ks[3];

	for (int i = 0; i < 3; ++i) {
		DES_set_key_unchecked(to_ccblock(key, i * 8), &ks[i]);
	}

	return crypt_generic_ecb<8>(ibuf, [&ks, &encrypt](const uint8_t *iblock, uint8_t *oblock) {
			DES_ecb3_encrypt(to_ccblock(iblock), to_cblock(oblock), &ks[0], &ks[1], &ks[2],
					encrypt ? DES_ENCRYPT : DES_DECRYPT);
	});
#else
	return crypt_generic(et_3des_ecb, ibuf, key, encrypt);
#endif
}

string crypt_des_ecb(const string& ibuf, const string& key, bool encrypt)
{
#if defined(BCM2UTILS_USE_OPENSSL)
	check_keysize(key, 8, "des-ecb");
	DES_key_schedule ks;
	DES_set_key_unchecked(to_ccblock(key, 0), &ks);

	return crypt_generic_ecb<8>(ibuf, [&ks, &encrypt](const uint8_t *iblock, uint8_t *oblock) {
			DES_ecb_encrypt(to_ccblock(iblock), to_cblock(oblock), &ks,
					encrypt ? DES_ENCRYPT : DES_DECRYPT);
	});
#else
	return crypt_generic(et_des_ecb, ibuf, key, encrypt);
#endif
}

string crypt_aes_256_ecb(const string& ibuf, const string& key, bool encrypt)
{
#if defined(BCM2UTILS_USE_OPENSSL)
	check_keysize(key, 32, "aes-256-ecb");
	AES_KEY aes = make_aes_key(key, encrypt);

	return crypt_generic_ecb<16>(ibuf, [&aes, &encrypt](const uint8_t *iblock, uint8_t *oblock) {
			if (encrypt) {
				AES_encrypt(iblock, oblock, &aes);
			} else {
				AES_decrypt(iblock, oblock, &aes);
			}
	});
#else
	return crypt_generic(et_aes_256_ecb, ibuf, key, encrypt);
#endif
}

#ifdef BCM2UTILS_USE_OPENSSL
namespace {
template<size_t BITS> string crypt_aes_cbc(const string& ibuf, const string& key_and_iv, bool encrypt)
{
	// first the key, then the iv
	check_keysize(key_and_iv, (BITS / 8) + 16, "aes-" + to_string(BITS) + "-cbc");

	string key = key_and_iv.substr(0, BITS / 8);
	string iv = key_and_iv.substr(BITS / 8);

	AES_KEY aes = make_aes_key(key, encrypt);
	string obuf = ibuf;
	AES_cbc_encrypt(data(ibuf), data(obuf), ibuf.size(), &aes, data(iv), encrypt);
	return obuf;
}
}

string crypt_aes_128_cbc(const string& ibuf, const string& key_and_iv, bool encrypt)
{
	return crypt_aes_cbc<128>(ibuf, key_and_iv, encrypt);
}
#else
string crypt_aes_128_cbc(const string& ibuf, const string& key_and_iv, bool encrypt)
{
	return crypt_generic(et_aes_128_cbc, ibuf, key_and_iv, encrypt);
}
#endif

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
		double r = rand_motorola();
		int x = ((r / 0x7fffffff) * 255) + 1;
		buf[i] ^= x;
	}

	return buf;
}

// ditto!
string crypt_sub_16x16(string buf, bool encrypt)
{
	for (size_t i = 0; i < (buf.size() / 16) * 16; i += 2) {
		unsigned k = i & 0xff;

		if (encrypt) {
			buf[i] = (buf[i] + k) & 0xff;
		} else {
			buf[i] = (buf[i] - k) & 0xff;
		}
	}

	for (size_t i = 0; (i + 1) < buf.size(); i += 2) {
		swap(buf[i], buf[i + 1]);
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

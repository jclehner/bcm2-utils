#include "crypto.h"
#ifdef __APPLE__
static int set_key(bool encrypt, const unsigned char *key, int bits, AES_KEY* akey)
{
	if (bits != 128 && bits != 256) {
		return -1;
	}

	CCCryptorStatus ret = CCCryptorCreate(
			encrypt ? kCCEncrypt : kCCDecrypt,
			bits == 256 ? kCCAlgorithmAES256 : kCCAlgorithmAES128,
			0,
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

inline int AES_set_encrypt_key(const unsigned char *key, const int bits, AES_KEY* akey)
{ return set_key(false, key, bits, akey); }

int AES_encrypt(const unsigned char* in, unsigned char* out, AES_KEY* akey)
{ return crypt(in, out, akey); }

int AES_decrypt(const unsigned char* in, unsigned char* out, AES_KEY* akey)
{ return crypt(in, out, akey); }
#endif

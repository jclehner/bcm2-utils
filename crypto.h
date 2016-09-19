#ifndef BCM2UTILS_CRYPTO_H
#define BCM2UTILS_CRYPTO_H
#include <string>
namespace bcm2utils {

std::string hash_md5_keyed(const std::string& buf, const std::string& key);
std::string crypt_aes_256_ecb(const std::string& buf, const std::string& key, bool encrypt);

}
#endif

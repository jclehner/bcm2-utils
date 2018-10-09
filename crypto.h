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

#ifndef BCM2UTILS_CRYPTO_H
#define BCM2UTILS_CRYPTO_H
#include <string>
namespace bcm2utils {

std::string hash_md5(const std::string& buf);

std::string crypt_aes_256_ecb(const std::string& buf, const std::string& key, bool encrypt);
std::string crypt_3des_ecb(const std::string& buf, const std::string& key, bool encrypt);

std::string crypt_motorola(std::string buf, const std::string& key);
std::string crypt_sub_16x16(std::string buf, bool encrypt);
std::string crypt_xor_char(std::string buf, const std::string& key);

}
#endif

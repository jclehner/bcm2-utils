/**
 * bcm2-utils
 * Copyright (C) 2016 Joseph Lehner <joseph.c.lehner@gmail.com>
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

#include <boost/crc.hpp>
#include <algorithm>
#include "nonvoldef.h"
#include "gwsettings.h"
#include "crypto.h"
using namespace std;
using namespace bcm2dump;
using namespace bcm2utils;

namespace bcm2cfg {
namespace {
string read_stream(istream& is)
{
	return string(std::istreambuf_iterator<char>(is), {});
}

string gws_checksum(string buf, const csp<profile>& p)
{
	return hash_md5(buf + (p ? p->md5_key() : ""));
}

unsigned log2(unsigned num)
{
	unsigned ret = 0;
	while (num >>= 1) {
		++ret;
	}

	return ret;
}

unsigned pow2(unsigned num)
{
	unsigned ret = 2;
	while (num--) {
		ret <<= 1;
	}
	return ret;
}

string gws_crypt(const string& buf, const string& key, int type, bool encrypt)
{
	if (type == BCM2_CFG_ENC_AES256_ECB) {
		return crypt_aes_256_ecb(buf, key, encrypt);
	} else if (type == BCM2_CFG_ENC_AES128_CBC) {
		return crypt_aes_128_cbc(buf, key, encrypt);
	} else if (type == BCM2_CFG_ENC_3DES_ECB) {
		return crypt_3des_ecb(buf, key, encrypt);
	} else if (type == BCM2_CFG_ENC_DES_ECB) {
		return crypt_des_ecb(buf, key, encrypt);
	} else if (type == BCM2_CFG_ENC_SUB_16x16) {
		return crypt_sub_16x16(buf, encrypt);
	} else if (type == BCM2_CFG_ENC_XOR) {
		return crypt_xor_char(buf, key);
	} else {
		throw runtime_error("invalid encryption type " + to_string(type));
	}
}

unsigned gws_enc_blksize(const csp<profile>& p)
{
	switch (p->cfg_encryption()) {
		case BCM2_CFG_ENC_AES256_ECB:
		case BCM2_CFG_ENC_AES128_CBC:
		case BCM2_CFG_ENC_SUB_16x16:
			return 16;
		case BCM2_CFG_ENC_DES_ECB:
		case BCM2_CFG_ENC_3DES_ECB:
			return 8;

		default:
			return 1;
	}
}

bool gws_unpad(string& buf, const csp<profile>& p)
{
	int pad = p->cfg_padding();
	unsigned blksize = gws_enc_blksize(p);
	bool pad_optional = p->cfg_flags() & BCM2_CFG_FMT_GWS_PAD_OPTIONAL;

	if (pad == BCM2_CFG_PAD_PKCS7 || pad == BCM2_CFG_PAD_ANSI_X9_23 || pad == BCM2_CFG_PAD_ANSI_ISH) {
		unsigned padnum = buf.back() + (pad == BCM2_CFG_PAD_ANSI_ISH ? 1 : 0);
		// add 16 to buf size to account for the checksum
		unsigned expected = blksize - (((buf.size() + 16) - padnum) % blksize);

		if (padnum == expected || (expected == 0 && padnum == blksize && !pad_optional)) {
			logger::d() << "padding=" << to_hex(buf.substr(buf.size() - padnum)) << endl;
			buf.resize(buf.size() - padnum);
			return true;
		}
	} else if (pad == BCM2_CFG_PAD_ZERO) {
		bool ok = true;
		for (char c : buf.substr(align_left(buf.size(), blksize), buf.size() % blksize)) {
			if (c) {
				ok = false;
				break;
			}
		}

		if (ok) {
			buf.resize(align_left(buf.size(), blksize));
			return true;
		}
	} else if (pad == BCM2_CFG_PAD_ZEROBLK || pad == BCM2_CFG_PAD_01BLK) {
		char ch = (pad == BCM2_CFG_PAD_ZEROBLK) ? 0x00 : 0x01;
		string blk(blksize, ch);

		if (buf.substr(buf.size() - blksize) == blk) {
			buf.resize(buf.size() - blksize);
			return true;
		}

		return false;
	} else {
		return false;
	}

	logger::v() << "failed to remove padding" << endl;
	return false;
}

string gws_decrypt(string buf, string& checksum, string& key, const csp<profile>& p, bool& padded)
{
	int flags = p->cfg_flags();
	int enc = p->cfg_encryption();

	logger::d() << "decrypting with profile " << p->name() << endl;

	if (flags & BCM2_CFG_FMT_GWS_LEN_PREFIX) {
		auto len = be_to_h(extract<uint32_t>(checksum));
		if (len == (buf.size() + 12)) {
			checksum.erase(0, 4);
			checksum.append(buf.substr(0, 4));
			buf.erase(0, 4);
		} else {
			logger::d() << "unexpected length prefix: " << len << endl;
		}
	} else if (flags & BCM2_CFG_FMT_GWS_CLEN_PREFIX) {
		if (checksum == "Content-Length: ") {
			auto pos = buf.find("\r\n\r\n");
			auto len = lexical_cast<uint32_t>(buf.substr(0, pos));
			auto beg = pos + 4;

			if (len != (buf.size() - beg)) {
				logger::d() << "unexpected length prefix: " << len << endl;
			}

			checksum = buf.substr(beg, 16);
			buf = buf.substr(beg + 16);
		} else {
			logger::d() << "length prefix is missing" << endl;
		}
	}

	if (flags & BCM2_CFG_FMT_GWS_FULL_ENC) {
		buf = checksum + buf;
	}

	if (enc == BCM2_CFG_ENC_MOTOROLA) {
		if (key.empty()) {
			key = buf.back();
		}
		buf = crypt_motorola(buf.substr(0, buf.size() - 1), key);
	} else {
		buf = gws_crypt(buf, key, enc, false);
	}

	padded = gws_unpad(buf, p);

	if (flags & BCM2_CFG_FMT_GWS_FULL_ENC) {
		checksum = buf.substr(0, 16);
		buf = buf.substr(16);
	}

	return buf;
}

string gws_encrypt(string buf, const string& key, const csp<profile>& p, bool pad)
{
	int flags = p->cfg_flags();
	int enc = p->cfg_encryption();

	if (flags & BCM2_CFG_FMT_GWS_FULL_ENC) {
		buf = gws_checksum(buf, p) + buf;
	}

	// TODO move all padding stuff to crypto.cc

	if (!(flags & BCM2_CFG_FMT_GWS_PAD_OPTIONAL) && !pad) {
		pad = true;
		logger::d() << "force-enabling padding" << endl;
	}

	if (enc == BCM2_CFG_ENC_MOTOROLA) {
		return crypt_motorola(buf, key) + key;
	} else if (enc != BCM2_CFG_ENC_NONE) {
		if (pad) {
			int padding = p->cfg_padding();
			if (padding == BCM2_CFG_PAD_ZEROBLK) {
				buf += string(16, '\0');
			} else {
				unsigned blksize = gws_enc_blksize(p);
				unsigned padnum = blksize - (buf.size() % blksize);

				if (padding == BCM2_CFG_PAD_ANSI_X9_23) {
					buf += string(padnum - 1, '\0');
					buf += char(padnum & 0xff);
				} else if (padding == BCM2_CFG_PAD_ANSI_ISH) {
					buf += string(padnum - 1, '\0');
					buf += char((padnum - 1) & 0xff);
				} else if (padding == BCM2_CFG_PAD_PKCS7) {
					buf += string(padnum, char(padnum & 0xff));
				} else if (padding == BCM2_CFG_PAD_ZERO) {
					buf += string(padnum, '\0');
				} else if (padding) {
					throw runtime_error("unknown padding type");
				}
			}
		}

		buf = gws_crypt(buf, key, enc, true);
	} else {
		throw user_error("profile " + p->name() + " does not support encryption");
	}

	if (!(flags & BCM2_CFG_FMT_GWS_FULL_ENC)) {
		buf = gws_checksum(buf, p) + buf;
	}

	if (flags & BCM2_CFG_FMT_GWS_LEN_PREFIX) {
		buf.insert(0, to_buf(h_to_be(buf.size())));
	} else if (flags & BCM2_CFG_FMT_GWS_CLEN_PREFIX) {
		buf.insert(0, "Content-Length: " + to_string(buf.size()) + "\r\n\r\n");
	}

	return buf;
}

string group_header_to_string(int format, const string& checksum, bool is_chksum_valid,
		size_t size, bool is_size_valid, const string& key, bool is_encrypted,
		const string& profile, bool is_auto_profile, const string& circumfix)
{
	ostringstream ostr;
	ostr << "type    : ";
	switch (format) {
	case nv_group::fmt_gws:
		ostr << "gwsettings";
		break;
	case nv_group::fmt_gwsdyn:
		ostr << "gwsdyn";
		break;
	case nv_group::fmt_dyn:
		ostr << "dyn";
		break;
	case nv_group::fmt_perm:
		ostr << "perm";
		break;
	case nv_group::fmt_boltenv:
		ostr << "boltenv";
		break;
	default:
		ostr << "(unknown)";
	}
	ostr << endl << "profile : ";
	if (profile.empty()) {
		ostr << "(unknown)" << endl;
	} else {
		ostr << profile << (is_auto_profile ? "" : " (forced)") << endl;
	}
	ostr << "checksum: " << checksum;
	if (!profile.empty() || format != nv_group::fmt_gws) {
		ostr << " " << (is_chksum_valid ? "(ok)" : "(bad)") << endl;
	} else {
		ostr << endl;
	}

	ostr << "size    : ";
	if (!is_encrypted || !key.empty()) {
		ostr << size << " " << (is_size_valid ? "(ok)" : "(bad)") << endl;
	} else {
		ostr << "(unknown)" << endl;
	}

	if (is_encrypted) {
		ostr << "key     : " << (key.empty() ? "(unknown)" : to_hex(key)) << endl;
	}

	if (!circumfix.empty()) {
		ostr << "circfix : " << to_hex(circumfix) << endl;
	}

	return ostr.str();
}

class permdyn : public encryptable_settings
{
	public:
	permdyn(int format, const csp<bcm2dump::profile>& p, const string& key)
	: encryptable_settings("permdyn", format, p), m_key(key) {}

	virtual size_t bytes() const override
	{ return m_size.num(); }

	virtual size_t data_bytes() const override
	{ return bytes() - 8; }

	virtual bool is_valid() const override
	{ return m_magic_valid; }

	virtual istream& read(istream& is) override
	{
		// TODO move this to settings::read()
		is.seekg(-16, ios::cur);

		auto beg = is.tellg();

		m_magic_valid = false;

		if (!m_size.read(is) || !m_checksum.read(is)) {
			throw runtime_error("failed to read header");
		}

		if (m_size.num() == 0xffffffff && m_checksum.num() == 0xffffffff) {
			// probably an old-style permnv/dynnv file, which starts
			// with a prefix of 202 0xff bytes (of which we've already read 8).

			string prefix(202 - 8, '\0');
			if (!is.read(&prefix[0], prefix.size())) {
				throw runtime_error("failed to read prefix");
			}

			if (prefix.find_first_not_of('\xff') != string::npos) {
				return is;
			}

			m_magic_valid = true;
			m_old_style = true;

			// seek to the footer
			is.seekg(-8, ios::end);
			m_raw_size = is.tellg();

			uint32_t segment_size;
			uint32_t segment_bitmask;

			nv_u32::read(is, segment_size);
			nv_u32::read(is, segment_bitmask);

			unsigned segment_index = -int32_t(segment_bitmask);

			streampos offset = 0;

			if (segment_size > m_raw_size || segment_size == 0xffffffff) {
				logger::w() << "invalid segment size: 0x" << to_hex(segment_size) << endl;
			} else {
				m_write_count = log2(segment_index) - 1;
				if (pow2(m_write_count) != segment_index) {
					logger::w() << "invalid segment bitmask: 0x" << to_hex(segment_bitmask) << endl;
					m_write_count = 0;
				} else {
					offset = segment_size * min(m_write_count, 16u);
					logger::d() << "write count: " << m_write_count << ", offset: " << offset << endl;
				}
			}

			if ((beg + offset) >= m_raw_size) {
				logger::w() << "segment offset " << offset << " exceeds maximum size " << m_raw_size << endl;
				offset = 0;
			}

			// seek to the beginning of the settings group data
			is.seekg(beg + offset + streampos(202));

			for (int i = 0; i < 2; ++i) {
				// re-read the checksum and size fields
				if (!m_size.read(is) || !m_checksum.read(is)) {
					throw runtime_error("failed to read header");
				}

				if (!i && (m_size.num() == 0xffffffff || m_size.num() > m_raw_size)) {
					// at least try to read the first copy, if we've messed up the calculations above
					logger::w() << "invalid data size " << m_size.num() << "; retrying at offset 0" << endl;
					is.seekg(beg + streampos(202));
				} else {
					break;
				}
			}

			// TODO try reading from the backup
		} else {
			m_old_style = false;
			// FIXME
			m_magic_valid = true;
		}

		string buf = read_stream(is);
		if (buf.size() < (m_size.num() - 8)) {
			logger::w() << type() << ": read " << buf.size() << "b, expected at least " << m_size.num() - 8 << endl;
			m_size_valid = false;
		} else {
			buf.resize(m_size.num() - 8);
			m_size_valid = true;
		}

		// minus 8, since m_size includes itself (4 bytes) plus the checksum (also 4 bytes)
		uint32_t checksum = calc_checksum(buf.substr(0, m_size.num() - 8));
		m_checksum_valid = checksum == m_checksum.num();

		if (!m_checksum_valid) {
			logger::d() << type() << ": checksum mismatch: " << to_hex(checksum) << " / " << to_hex(m_checksum.num()) << endl;
		}

		istringstream istr(buf);
		settings::read(istr);

		if (!key().empty()) {
			// a key was specified, but we must check if the file is actually encrypted. compared with
			// the GatewaySettings there's no easy way to do this, as there's no magic we can check for.
			//
			// we thus parse the data twice, once as-is, and once decrypted with the supplied key,
			// and check which yields more settings groups. if there's only one group in both cases,
			// do a sanity check on the version

			auto unenc_groups = parts();

			m_parts.clear();
			istr.str(crypt_aes_256_ecb(istr.str(), key(), false));
			settings::read(istr);

			if (unenc_groups.size() > parts().size()) {
				// more groups when not decrypted -> file isn't encrypted
				m_key.clear();
			} else if (unenc_groups.size() == m_parts.size() && m_parts.size() == 1) {
				// one part in both cases
				auto unenc_ver = nv_val_cast<nv_group>(unenc_groups[0].val)->version();
				auto enc_ver = nv_val_cast<nv_group>(parts()[0].val)->version();

				if (enc_ver.major() > 5 || enc_ver.minor() > 100) {
					// assume that file isn't encrypted, as major versions
					// are usually 0 or 1. the limit on the minor version
					// should be good for most cases too.
					m_key.clear();
				}
			}

			if (m_key.empty()) {
				groups(unenc_groups);
			}
		}

		return is;
	}


	virtual ostream& write(ostream& os) const override
	{
		ostringstream ostr;
		settings::write(ostr);

		string buf = ostr.str();

		if (!key().empty()) {
			buf = crypt_aes_256_ecb(buf, key(), true);
		}

		ostr.str("");

		if (m_old_style) {
			ostr << string(202, '\xff');
		}

		nv_u32::write(ostr, 8 + buf.size());
		nv_u32::write(ostr, calc_checksum(buf));

		ostr.write(buf.data(), buf.size());

		os << ostr.str();

		if (m_old_style) {
			// currently, this function simply writes a file that pretends to be
			// a) a nonvol file that has been written just once
			// b) uses the same data for both the primary and backup
			//
			// TODO actually append our data

			// set offset of the backup data to the end of the primary data
			size_t segment_size = ostr.tellp();

			// 8 bytes for the footer are already subtracted
			ssize_t diff = m_raw_size - segment_size;

			// we've written the primary data, but still need to write the backup data.
			// first, check if it would even fit!

			if (segment_size < diff) {
				// backup data actually fits. we could probably just use `segment_size`, but
				// all firmwares seem to include at least some padding in between, and
				// some of those align the offset to 0x1000 (or 0x100?).

				if (align_left(segment_size, 0x1000) < diff) {
					segment_size = align_right(segment_size, 0x1000);
				} else if (align_left(segment_size, 0x100) < diff) {
					segment_size = align_right(segment_size, 0x100);
				}

				// write padding between segments
				os << string(segment_size - ostr.tellp(), '\xff');

				// write backup segment
				os << ostr.str();

				diff -= segment_size;
			} else {
				logger::i() << "no space to fit backup data" << endl;
			}

			if (diff < 0) {
				throw runtime_error("file size exceeds maximum of " + ::to_string(m_raw_size));
			}

			// pad to size
			os << string(diff, '\xff');

			nv_u32::write(os, segment_size);
			// pretend that this is a file that has been written once
			nv_u32::write(os, 0xfffffffc);
		}

		if (!os) {
			throw runtime_error("write error");
		}

		return os;
	}

	virtual string header_to_string() const override
	{
		return group_header_to_string(m_format, to_hex(m_checksum.num()), m_checksum_valid,
				m_size.num(), m_size_valid, "", false, "", false, "");
	}

	virtual bool padded() const override
	{ return false; }

	virtual void padded(bool) override
	{}

	virtual string key() const override
	{ return m_key; }

	virtual void key(const string& key) override
	{ m_key = key; }

	private:
	static uint32_t calc_checksum(const string& buf)
	{
		uint32_t remaining = buf.size();
		// the checksum is calculated from the header (u32 size, u32 checksum), with
		// the checksum part set to 0, followed by the data buffer. setting the initial
		// sum to buf.size() + 8 (since buf does NOT contain the header) has the same effect.
		uint32_t sum = buf.size() + 8;

		while (remaining >= 4) {
			sum += be_to_h(extract<uint32_t>(buf.substr(buf.size() - remaining, 4)));
			remaining -= 4;
		}

		uint16_t half = 0;

		if (remaining >= 2) {
			half = be_to_h(extract<uint16_t>(buf.substr(buf.size() - remaining, 2)));
			remaining -= 2;
		}

		uint8_t byte = 0;

		if (remaining) {
			byte = extract<uint8_t>(buf.substr(buf.size() - remaining, 1));
		}

		sum += ((byte | (half << 8)) << 8);

		return ~sum;
	}

	nv_u32 m_size;
	nv_u32 m_checksum;
	string m_key;
	unsigned m_write_count = 0;
	uint32_t m_raw_size = 0;
	bool m_checksum_valid = false;
	bool m_magic_valid = false;
	bool m_old_style = true;
	bool m_size_valid = false;
};

class gwsettings : public encryptable_settings
{
	public:
	gwsettings(const string& checksum, const csp<bcm2dump::profile>& p,
			const string& key, const string& pw)
	: encryptable_settings("gwsettings", nv_group::fmt_gws, p),
	  m_checksum(checksum), m_key(key), m_pw(pw) {}

	virtual size_t bytes() const override
	{ return m_size.num(); }

	virtual size_t data_bytes() const override
	{ return bytes() - (m_magic.size() + 6); }

	virtual string type() const override
	{ return "gwsettings"; }

	virtual bool is_valid() const override
	{ return m_magic_valid; }

	virtual void key(const string& key) override
	{ m_key = key; }

	virtual string key() const override
	{ return m_key; }

	virtual void padded(bool padded) override
	{ m_padded = padded; }

	virtual bool padded() const override
	{ return m_padded; }

	virtual istream& read(istream& is) override
	{
		string buf = read_stream(is);

		m_checksum_valid = false;

		clip_circumfix(buf);
		validate_checksum_and_detect_profile(buf);
		validate_magic(buf);
		m_encrypted = !m_magic_valid;

		if (!m_magic_valid && !decrypt_and_detect_profile(buf)) {
			m_key = m_pw = "";
			return is;
		} else if (!m_encrypted) {
			m_key = m_pw = "";
		}

		istringstream istr(buf.substr(m_magic.size()));
		read_header(istr, buf.size());
		settings::read(istr);
		return is;
	}

	virtual ostream& write(ostream& os) const override
	{
		if (!profile()) {
			throw runtime_error("cannot write file without a profile");
		}

		ostringstream ostr;
		settings::write(ostr);
		string buf = ostr.str();

		ostr.str("");
		ostr.write(m_magic.data(), m_magic.size());
		m_version.write(ostr);
#if 1
		// 2 bytes for version, 4 for size
		nv_u32::write(ostr, m_magic.size() + 6 + buf.size());
#else
		m_size.write(ostr);
#endif

		buf = ostr.str() + buf;

		if (!m_key.empty()) {
			buf = gws_encrypt(buf, m_key, m_profile, m_padded);
		} else {
			buf = gws_checksum(buf, m_profile) + buf;
		}

		buf = m_circumfix + buf + m_circumfix;

		if (!(os.write(buf.data(), buf.size()))) {
			throw runtime_error("error while writing data");
		}

		return os;
	}

	virtual string header_to_string() const override
	{
		return group_header_to_string(m_format, to_hex(m_checksum), m_checksum_valid,
				m_size.num(), m_size_valid, m_key, m_encrypted, profile() ? profile()->name() : "",
				m_is_auto_profile, m_circumfix);
	}

	private:
	string m_checksum;

	void clip_circumfix(string& buf)
	{
		string top = m_checksum.substr(0, 12);
		string btm = buf.substr(buf.size() - 12, 12);

		if (top == btm) {
			m_circumfix = top;
			m_checksum = m_checksum.substr(12) + buf.substr(0, 12);
			buf = buf.substr(12, buf.size() - 24);
		}
	}

	void validate_checksum_and_detect_profile(const string& buf)
	{

		if (this->profile()) {
			validate_checksum(buf, this->profile());
		} else {
			for (auto p : profile::list()) {
				if (validate_checksum(buf, p)) {
					m_is_auto_profile = true;
					m_profile = p;
					break;
				}
			}
		}
	}

	bool validate_checksum(const string& buf, const csp<bcm2dump::profile>& p)
	{
		m_checksum_valid = (m_checksum == gws_checksum(buf, p));
		return m_checksum_valid;
	}

	bool validate_magic(const string& buf)
	{
		// The magic values on Sagem 3686 modems (and possibly others) are ISP-dependent:
		//
		// FAST3686<isp>056t9p48jp4ee6u9ee659jy9e-54e4j6r0j069k-056
		//
		// Currently known values for <isp>
		// DNA
		// CLARO
		// SFR-PC20
		const string magic2_part2 = "056t9p48jp4ee6u9ee659jy9e-54e4j6r0j069k-056";

		const vector<string> magics {
				"6u9E9eWF0bt9Y8Rw690Le4669JYe4d-056T9p4ijm4EA6u9ee659jn9E-54e4j6rPj069K-670",
				"6u9e9ewf0jt9y85w690je4669jye4d-" + magic2_part2,
				"6u9e9ewf0jt9y85w690je4669jye4d-056t9p48jp4ee6u9ee659jy9e-54e4j6r0j069k-057",
		};

		for (const string& magic : magics) {
			if (starts_with(buf, magic)) {
				m_magic_valid = true;
				m_magic = magic;
				break;
			}
		}

		if (!m_magic_valid) {
			auto pos = buf.find(magic2_part2);
			if (pos != string::npos) {
				m_magic_valid = true;
				m_magic = buf.substr(0, pos + magic2_part2.size());
			} else {
				auto it = find_if(buf.begin(), buf.end(), [] (char c) {
						return c != '-' && !isalnum(c);
				});

				if (it != buf.end()) {
					const auto longest = max_element(magics.begin(), magics.end(), [] (auto a, auto b) {
							return a.size() < b.size();
					});

					string magic(buf.begin(), it);
					if (magic.size() >= magic2_part2.size() && magic.size() <= longest->size()) {
						logger::v() << "magic detected by brute force" << endl;
						m_magic_valid = true;
						m_magic = magic;
					}
				}
			}
		}

		return m_magic_valid;
	}

	void read_header(istringstream& istr, size_t bufsize)
	{
		m_size.num(0);

		if (!m_version.read(istr) || !m_size.read(istr)) {
			throw runtime_error("error while reading header");
		}

		logger::t("magic=%s\n", m_magic.c_str());

		auto v = m_version.num();
		logger::t("version=%d.%d, size=%d\n", v >> 8, v & 0xff, m_size.num());

		m_size_valid = m_size.num() == bufsize;

		if (!m_size_valid && bufsize > m_size.num()) {
			logger::v() << "data size exceeds reported file size" << endl;
			m_size.num(bufsize);
		}
	}

	bool decrypt_with_profile(string& buf, const csp<bcm2dump::profile>& p)
	{
		if (!p || !p->cfg_encryption()) {
			return false;
		}

		vector<string> keys;

		if (!m_key.empty()) {
			keys.push_back(m_key);
		} else if (!m_pw.empty()) {
			keys.push_back(p->derive_key(m_pw));
		} else {
			keys = p->default_keys();
			// in case the encryption mode does not require a key
			keys.push_back("");
		}

		for (auto key : keys) {
			string tmpsum = m_checksum;
			string tmpbuf;
			bool padded;

			try {
				tmpbuf = gws_decrypt(buf, tmpsum, key, p, padded);
			} catch (const invalid_argument& e) {
				logger::t() << e.what() << endl;
				continue;
			}

			if (validate_magic(tmpbuf)) {
				m_key = key;
				buf = tmpbuf;
				m_padded = padded;

				if (!m_checksum_valid) {
					m_checksum = tmpsum;
					validate_checksum(buf, p);
				}

				return true;
			}
		}

		return false;
	}

	bool decrypt_and_detect_profile(string& buf)
	{
		if (profile()) {
			bool ok = decrypt_with_profile(buf, profile());

			if (!m_is_auto_profile || ok) {
				return ok;
			}
		}

		for (auto p : profile::list()) {
			if (decrypt_with_profile(buf, p)) {
				m_is_auto_profile = true;
				m_profile = p;
				return true;
			}
		}

		return false;
	}

	bool m_is_auto_profile = false;
	bool m_checksum_valid = false;
	bool m_magic_valid = false;
	bool m_size_valid = false;
	bool m_encrypted = false;
	nv_version m_version;
	nv_u32 m_size;
	string m_magic;
	string m_key;
	string m_pw;
	string m_circumfix;
	bool m_padded = false;
};

static constexpr uint8_t header_size = 0x1b;
// big endian
static constexpr uint32_t tlv_cheat = 0x011a0000;
// little endian
static constexpr uint32_t magic = 0xbabefeed;

class boltenv : public encryptable_settings
{
	public:
	class var : public nv_compound
	{
		public:
		var() : nv_compound(false, "boltenv-var"), m_value(make_shared<nv_zstring>()), m_size(0)
		{
			m_parts.push_back({ "value", m_value });
		}

		istream& read_data(istream& is, size_t size)
		{
			string data(size, '\0');
			is.read(&data[0], data.size());

			auto tok = split(data, '=', true, 2);
			if (!tok.empty()) {
				m_key = tok[0];
			} else {
				logger::d() << "warning: empty boltenv variable" << endl;
			}

			// even an empty variable should be encoded as "NAME=", but we never know
			if (tok.size() == 2) {
				m_value->parse(tok[1]);
			} else if (m_key.empty()) {
				m_value->parse(data);
			}

			m_set = true;

			return is;
		}

		ostream& write_data(ostream& os) const
		{
			string data(m_key + "=" + m_value->str());
			// this can happen if the user sets a value using "NAME.value" instead of just "NAME"
			if (data.size() > max_size()) {
				throw runtime_error("variable size out of bounds");
			}

			return os.write(data.data(), data.size());
		}

		const string& key() const { return m_key; }

		virtual size_t bytes() const override
		{ return m_size; }

		virtual bool parse(const string& str) override
		{
			size_t new_size = calc_size(str.size());
			if (new_size <= max_size() && m_value->parse(str)) {
				m_size = new_size;
				m_set = true;
				return true;
			}
			return false;
		}

		virtual std::string type() const override
		{ return "boltenv-var"; }

		protected:
		virtual size_t calc_size(size_t size) const = 0;
		virtual size_t max_size() const = 0;

		virtual list definition() const override
		{ throw runtime_error(__PRETTY_FUNCTION__); }

		private:
		sp<nv_zstring> m_value;
		size_t m_size;
		string m_key;
	};

	template<class PrefixType> class var_base : public var
	{
		public:
		typedef typename PrefixType::num_type prefix_num_type;

		istream& read_size(istream& is)
		{
			return PrefixType::read(is, m_size);
		}

		ostream& write_size(ostream& os) const
		{
			return PrefixType::write(os, m_size);
		}

		virtual size_t bytes() const override
		{
			return m_size;
		}

		protected:
		virtual size_t max_size() const override
		{ return PrefixType::max; }

		private:
		prefix_num_type m_size;
	};

	class var1 : public var_base<nv_u8>
	{
		public:
		var1() : m_flags(make_shared<nv_u8>())
		{
			m_parts.push_back({ "flags", m_flags });
		}

		virtual istream& read(istream& is) override
		{
			if (!read_size(is) || !bytes()) {
				return is;
			}

			m_flags->read(is);
			return read_data(is, bytes() - 1);
		}

		virtual ostream& write(ostream& os) const override
		{
			os << '\x01';
			write_size(os);
			m_flags->write(os);
			return write_data(os);
		}

		protected:
		virtual size_t calc_size(size_t size) const override
		{
			// flags (1 byte), then key, then '=', then value
			return (1 + key().size() + 1 + size);
		}

		private:
		sp<nv_u8> m_flags;
	};

	class var2 : public var_base<nv_u16>
	{
		public:
		var2() {}

		virtual istream& read(istream& is) override
		{
			if (!read_size(is) || !bytes()) {
				return is;
			}

			return read_data(is, bytes());
		}

		virtual ostream& write(ostream& os) const override
		{
			os << '\x02';
			write_size(os);
			return write_data(os);
		}

		protected:
		virtual size_t calc_size(size_t size) const override
		{
			// key, then '=', then value
			return (key().size() + 1 + size);
		}
	};

	boltenv(const csp<bcm2dump::profile>& p, const string& key)
	: encryptable_settings("boltenv", nv_group::fmt_boltenv, p), m_key(key)
	{}

	virtual size_t bytes() const override
	{ return m_full_size; }

	virtual size_t data_bytes() const override
	{ return m_data_bytes; }

	virtual string type() const override
	{ return "boltenv"; }

	virtual bool is_valid() const override
	{ return m_valid; }

	virtual void key(const string& key) override
	{ m_key = key; }

	virtual string key() const override
	{ return m_key; }

	virtual void padded(bool padded) override
	{ m_padded = padded; }

	virtual bool padded() const override
	{ return m_padded; }

	virtual istream& read(istream& is) override
	{
		string buf = read_stream(is);
		if (!m_key.empty()) {
			buf = crypt_aes_256_ecb(buf, m_key, false);
		}

		m_full_size = buf.size();

		do {
			istringstream istr(buf);

			uint32_t n;
			if (!nv_u32::read(istr, n) || n != tlv_cheat) {
				break;
			}

			if (!nv_u32le::read(istr, n) || n != magic) {
				break;
			}

			m_valid = true;

			nv_u32le::read(istr, m_unknown1);
			nv_u32le::read(istr, m_unknown2);
			nv_u32le::read(istr, m_write_count);
			nv_u32le::read(istr, m_data_bytes);
			nv_u32le::read(istr, m_checksum);

			// FIXME
			m_checksum_valid = true;

			if (!istr) {
				break;
			}

			uint32_t data_bytes = 0;

			nv_compound::list parts;

			while(data_bytes < m_data_bytes) {
				int type = istr.get();
				if (type == EOF) {
					logger::d() << "encountered premature EOF" << endl;
					break;
				} else if (type == 0x00) {
					data_bytes += 1;
					break;
				} else if (type == 0x01 || type == 0x02) {
					sp<var> var;

					if (type == 0x01) {
						var = make_shared<var1>();
					} else {
						var = make_shared<var2>();
					}

					var->read(istr);

					logger::d() << "var: " << var->key() << ", size " << var->bytes() << ", type " << type << endl;

					parts.push_back({ var->key(), var });
					data_bytes += var->bytes() + 1;
				} else {
					throw runtime_error("unknown data type: " + to_hex(type));
				}
			}

			logger::d() << "m_data_bytes " << m_data_bytes << ", read " << data_bytes << endl;

			// FIXME account for the prefix lengths
			//m_data_bytes_valid = data_bytes == m_data_bytes;

			groups(parts);

			return is;
		} while (false);

		throw runtime_error("failed to parse header");
	}

	virtual ostream& write(ostream& os) const override
	{
		ostringstream ostr(ios::ate);

		nv_u32::write(ostr, tlv_cheat);
		nv_u32le::write(ostr, magic);
		nv_u32le::write(ostr, m_unknown1);
		nv_u32le::write(ostr, m_unknown2);
		nv_u32le::write(ostr, m_write_count + 1);

		ostringstream data;
		nv_compound::write(data);
		// write end of data marker
		data << '\0';

		string databuf = data.str();

		nv_u32le::write(ostr, databuf.size());
		boost::crc_32_type crc;
		crc.process_bytes(databuf.data(), databuf.size());
		nv_u32le::write(ostr, crc.checksum());

		ostr.write(databuf.data(), databuf.size());

		// pad to 16 bytes, even if no encryption is used
		if (ostr.tellp() % 16) {
			ostr << string(16 - (ostr.tellp() % 16), '\0');
		}

		if (!m_key.empty()) {
			ostr.str(crypt_aes_256_ecb(ostr.str(), m_key, true));
		}

		if (ostr.tellp() > m_full_size) {
			throw runtime_error("new file size would exceed " + ::to_string(m_full_size) + " bytes");
		} else {
			ostr << string(m_full_size - ostr.tellp(), '\xff');
		}

		return os << ostr.str();
	}

	virtual string header_to_string() const override
	{
		return group_header_to_string(m_format, to_hex(m_checksum), m_checksum_valid,
				m_data_bytes, m_data_bytes_valid, m_key, !m_key.empty(), "", false, "");
	}

	private:
	uint32_t m_unknown1, m_unknown2;
	uint32_t m_write_count;
	uint32_t m_data_bytes;
	uint32_t m_full_size;
	uint32_t m_checksum;

	bool m_checksum_valid = false;
	bool m_data_bytes_valid = false;

	std::string m_key;
	bool m_valid = false;
	bool m_padded = false;
};
}

istream& settings::read(istream& is)
{
	m_groups.clear();

	sp<nv_group> group;
	size_t remaining = data_bytes();
	unsigned mult = 1;

	while (remaining >= 8 && !is.eof()) {
		if (!nv_group::read(is, group, m_format, remaining, m_profile) || !group) {
			if (is.eof() || !group) {
				break;
			}

			throw runtime_error("failed to read group " + (group ? group->magic().to_str() : ""));
		} else {
			string name = group->name();
			if (find(name)) {
				name += "_" + std::to_string(++mult);
				logger::v() << "redefinition of " << group->name() << " renamed to " << name << endl;
			}
			m_groups.push_back( { name, group });
			remaining -= group->bytes();
		}
	}

	return is;
}

ostream& settings::write(ostream& os) const
{
	return nv_compound::write(os);
}

void settings::remove(const string& name)
{
	auto it = find_if(m_groups.begin(), m_groups.end(), [name](const nv_val::named& v) { return v.name == name; });
	if (it != m_groups.end()) {
		m_groups.erase(it);
	} else {
		throw user_error("no such group: " + name);
	}
}

sp<settings> settings::read(istream& is, int format, const csp<bcm2dump::profile>& p, const string& key,
		const string& pw)
{
	sp<settings> ret;
	string start(16, '\0');

	if (format != nv_group::fmt_gwsdyn && format != nv_group::fmt_boltenv) {
		if (!is.read(&start[0], start.size())) {
			throw runtime_error("failed to read file");
		}

		if (format == nv_group::fmt_unknown) {
			if (start == string(16, '\xff')) {
				format = nv_group::fmt_dyn;
			} else {
				format = nv_group::fmt_gws;
			}
		}
	}

	if (format == nv_group::fmt_boltenv) {
		ret = make_shared<boltenv>(p, key);
	} else if (format != nv_group::fmt_gws) {
		ret = sp<permdyn>(new permdyn(format, p, key));
	} else {
		// if this is in fact a gwsettings type file, then start already contains the checksum
		ret = sp<gwsettings>(new gwsettings(start, p, key, pw));
	}

	if (ret) {
		ret->read(is);
	}

	return ret;
};
}

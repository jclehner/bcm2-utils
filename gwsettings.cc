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

#include <algorithm>
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
			buf.resize(buf.size() - padnum);
			return true;
		}
	} else if (pad == BCM2_CFG_PAD_ZEROBLK) {
		string zeroblk(blksize, '\0');

		if (buf.substr(buf.size() - blksize) == zeroblk) {
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
		auto len = ntoh(extract<uint32_t>(checksum));
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
		buf.insert(0, to_buf(htonl(buf.size())));
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

class permdyn : public settings
{
	public:
	permdyn(int format, const csp<bcm2dump::profile>& p)
	: settings("permdyn", format, p) {}

	virtual size_t bytes() const override
	{ return m_size.num(); }

	virtual size_t data_bytes() const override
	{ return bytes() - 8; }

	virtual bool is_valid() const override
	{ return m_magic_valid; }

	virtual istream& read(istream& is) override
	{
		if (m_format != nv_group::fmt_gwsdyn) {
			// actually, there's 16 more bytes at the beginning, but these have already been read
			// by gwsettings::read_file, and determined to be all \xff
			string magic(0xba, '\0');
			if (!is.read(&magic[0], magic.size()) || !m_size.read(is) || !m_checksum.read(is)) {
				throw runtime_error("failed to read header");
			}

			if (magic.find_first_not_of('\xff') != string::npos) {
				m_magic_valid = false;
				is.clear(ios::failbit);
				return is;
				//throw runtime_error("found non-0xff byte in magic");
			}
		} else {
			if (!m_size.read(is) || !m_checksum.read(is)) {
				throw runtime_error("failed to read header");
			}

			logger::t() << "m_size=" << m_size.num() << ", m_checksum=" << m_checksum.num() << endl;
		}

		m_magic_valid = true;

		string buf = read_stream(is);
		if (buf.size() < (m_size.num() - 8)) {
			logger::w() << type() << ": read " << buf.size() << "b, expected at least " << m_size.num() - 8 << endl;
		}

		// minus 8, since m_size includes itself (4 bytes) plus the checksum (also 4 bytes)
		uint32_t checksum = calc_checksum(buf.substr(0, m_size.num() - 8));
		m_checksum_valid = checksum == m_checksum.num();

		if (!m_checksum_valid) {
			logger::t() << type() << ": checksum mismatch: " << to_hex(checksum) << " / " << to_hex(m_checksum.num()) << endl;
		}

		m_footer = m_size.num() < buf.size() ? buf.substr(m_size.num()) : "";
		m_raw_size = buf.size();

		if (!m_format) {
			if (m_footer.size() >= 8) {
				uint32_t a, b;
				a = ntoh(extract<uint32_t>(m_footer.substr(m_footer.size() - 8, 4)));
				b = ntoh(extract<uint32_t>(m_footer.substr(m_footer.size() - 4, 4)));

				if (a == 0x5544 && b == 0xfffffffe) {
					m_format = nv_group::fmt_perm;
				} else if ((a == 0x10000 && b == 0xfffffffe) || (a == 0x8000 && b == 0xfffffffc)) {
					m_format = nv_group::fmt_dyn;
				} else {
					logger::t() << "a=0x" << to_hex(a) << ", b=0x" << to_hex(b) << endl;
				}
			}

			if (!m_format) {
				logger::w() << "failed to detect file format; please specify `-f perm` or `-f dyn`" << endl;
			}
		}

		istringstream istr(buf.substr(0, m_size.num()));
		settings::read(istr);

		return is;
	}

	virtual ostream& write(ostream& os) const override
	{
		ostringstream ostr;
		settings::write(ostr);
		string buf = ostr.str();

		if (m_format != nv_group::fmt_gwsdyn && !(os << string(0xca, '\xff'))) {
			throw runtime_error("failed to write magic");
		}

		if (!nv_u32::write(os, 8 + buf.size()) || !nv_u32::write(os, calc_checksum(buf))) {
			throw runtime_error("failed to write header");
		}

		if (!os.write(buf.data(), buf.size())) {
			throw runtime_error("failed to write data");
		}

		string footer = m_footer;
		ssize_t diff = (buf.size() + footer.size()) - m_raw_size;

		if (diff < 0) {
			// new size is smaller, pad with \xff
			footer.insert(0, string(-diff, '\xff'));
		} else if (diff) {
			// new size is larger, so we truncate m_footer
			if (diff < m_footer.size()) {
				footer.erase(0, diff);
			} else {
				logger::w() << "growing file to fit new data" << endl;
				footer.clear();
			}
		}

		if (!os.write(footer.data(), footer.size())) {
			throw runtime_error("failed to write footer");
		}

		return os;
	}

	virtual string header_to_string() const override
	{
		return group_header_to_string(m_format, to_hex(m_checksum.num()), m_checksum_valid,
				m_size.num(), true, "", false, "", false, "");
	}

	private:
	static uint32_t calc_checksum(const string& buf)
	{
		uint32_t remaining = buf.size();
		// the checksum is calculated from the header (u32 size, u32 checksum), with
		// the checksum part set to 0, followed by the data buffer. setting the initial
		// sum to buf.size() + 8 (since buf does NOT contain the header) has the same effect.
		uint32_t sum = buf.size() + 8;

		while (remaining >= 4) {
			sum += ntoh(extract<uint32_t>(buf.substr(buf.size() - remaining, 4)));
			remaining -= 4;
		}

		uint16_t half = 0;

		if (remaining >= 2) {
			half = ntoh(extract<uint16_t>(buf.substr(buf.size() - remaining, 2)));
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
	string m_footer;
	uint32_t m_raw_size = 0;
	bool m_checksum_valid = false;
	bool m_magic_valid = false;
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
		for (int n : { 74, 59, 58, 54 }) {
			if (do_validate_magic(buf.substr(0, n))) {
				return true;
			}
		}

		return false;
	}

	bool do_validate_magic(const string& magic)
	{
		m_magic = magic;
		m_magic_valid = (magic.end() == std::find_if(magic.begin(), magic.end(),
				[](char c) -> bool { return c != '-' && !isalnum(c); }));
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

// Currently known magic values:
// 6u9E9eWF0bt9Y8Rw690Le4669JYe4d-056T9p4ijm4EA6u9ee659jn9E-54e4j6rPj069K-670 (Technicolor, Thomson)
// 6u9e9ewf0jt9y85w690je4669jye4d-056t9p48jp4ee6u9ee659jy9e-54e4j6r0j069k-056 (Netgear, Motorola)
// FAST3686DNA056t9p48jp4ee6u9ee659jy9e-54e4j6r0j069k-056 (Sagemcom F@ST 3686 AC from DNA Oyj (ISP))
}

istream& settings::read(istream& is)
{
	if (m_is_raw) {
		m_raw_data = read_stream(is);
		return is;
	}

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
	if (!m_is_raw) {
		return nv_compound::write(os);
	} else {
		return os.write(m_raw_data.data(), m_raw_data.size());
	}
}

sp<settings> settings::read(istream& is, int format, const csp<bcm2dump::profile>& p, const string& key,
		const string& pw, bool raw)
{
	sp<settings> ret;
	string start(16, '\0');

	if (format != nv_group::fmt_gwsdyn) {
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

	if (format != nv_group::fmt_gws) {
		ret = sp<permdyn>(new permdyn(format, p));
	} else {
		// if this is in fact a gwsettings type file, then start already contains the checksum
		ret = sp<gwsettings>(new gwsettings(start, p, key, pw));
	}

	if (ret) {
		ret->raw(raw);
		ret->read(is);
	}

	return ret;
}





}

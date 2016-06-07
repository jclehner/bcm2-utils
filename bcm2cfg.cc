#include <openssl/aes.h>
#include <openssl/md5.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <stdexcept>
#include <streambuf>
#include <typeinfo>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string>
#include "profile.h"
#include "common.h"
#include "nonvol.h"
using namespace std;

namespace {
class error : public runtime_error
{
	public:
	error(const string& what) : runtime_error(what.c_str()) {}

};

class user_error : public runtime_error
{
	public:
	user_error(const string& what) : runtime_error(what.c_str()) {}
};

string parse_hex(const string& hexstr)
{
	if (hexstr.size() % 2) {
		throw user_error("invalid hex-string size "
				+ to_string(hexstr.size()));
	}

	string ret;
	ret.reserve(hexstr.size() / 2);

	for (size_t i = 0; i < hexstr.size(); i += 2) {
		string chr = hexstr.substr(i, 2);
		int c;
		if (sscanf(chr.c_str(), "%02x", &c) != 1) {
			throw user_error("invalid hex char '" + chr + "'");
		}

		ret += c;
	}

	return ret;
}

string to_hex(const string& data)
{
	ostringstream ostr;

	for (char c : data) {
		char h[3];
		sprintf(h, "%02x", c & 0xff);
		ostr << h;
		//ostr << ios::hex << setw(2) << setfill('0') << unsigned(c & 0xff);
	}

	return ostr.str();
}

class settings
{
	public:
	~settings()
	{
		free_groups();
	}

	void set_profile(const bcm2_profile *profile)
	{
		m_profile = profile;
		if (profile) {
			m_forced_profile = true;
		}
	}

	void set_padding()
	{ m_has_padding = true; }

	void set_key(const string &key, bool hexstr)
	{
		m_password.clear();
		m_key = hexstr ? parse_hex(key) : key;
		if (m_key.size() != 32) {
			throw user_error("invalid key size " + to_string(m_key.size()));
		}
	}

	void set_password(const string& password)
	{
		m_key.clear();
		m_password = password;
	}

	bool has_password() const
	{
		return !m_password.empty();
	}

	bool has_key() const
	{
		return !m_key.empty();
	}

	bool has_pw_or_key() const
	{
		return has_password() || has_key();
	}

	bool is_size_valid() const
	{
		return m_size_valid;
	}

	bool is_magic_valid() const
	{
		return m_magic_valid;
	}

	bool is_checksum_valid() const
	{
		return m_checksum_valid;
	}

	bool has_auto_profile() const
	{
		return m_auto_profile;
	}

	bool has_forced_profile() const
	{
		return m_forced_profile;
	}

	bool is_encrypted() const
	{
		return m_encrypted;
	}

	bool has_padding() const
	{
		return m_has_padding;
	}

	string get_checksum() const
	{
		return to_hex(m_fbuf.substr(0, 16));
	}

	string get_version() const
	{
		return "";
	}

	string get_key() const
	{
		return to_hex(m_key);
	}

	uint16_t get_reported_size() const
	{
		return m_reported_size;
	}

	size_t get_file_size() const
	{
		return m_fbuf.size();
	}

	uint16_t get_data_offset() const
	{
		return c_data_offset;
	}

	void read(const string& filename)
	{
		ifstream in;
		in.exceptions(ios::failbit | ios::badbit);

		try {
			in.open(filename.c_str());
		} catch (const exception& e) {
			throw user_error("failed to open input file");
		}

		in.seekg(0, ios::end);
		if (in.tellg() < c_data_offset) {
			throw user_error("file too small to be a config file");
		}

		m_fbuf.reserve(in.tellg());
		in.seekg(0, ios::beg);
		m_fbuf.assign(istreambuf_iterator<char>(in), istreambuf_iterator<char>());

		check_file();

		if (m_encrypted) {
			decrypt();
		} else {
			m_dbuf.assign(m_fbuf.substr(16));
		}

		if (!m_magic_valid) {
			check_header();
		}

		parse_data();
	}

	void write(const string& filename, bool encrypt = false)
	{
		ofstream out;
		out.exceptions(ios::failbit | ios::badbit);
		out.open(filename.c_str());

		if (m_dbuf.size() > 0xffff) {
			throw user_error("cannot write file - size would exceed 64k");
		}

		write16(74 + 4, m_dbuf.size());

		m_fbuf.clear();
		m_fbuf.append(string(16, '\0'));

		if (!encrypt) {
			m_fbuf.append(m_dbuf);
		} else {
			if (!m_password.empty()) {
				derive_key(m_profile);
			}

			if (m_key.empty()) {
				throw user_error("no encryption key specified");
			}

			m_fbuf.append(crypt(m_dbuf, true));
		}

		file_buf_md5(&m_fbuf[0]);
		out.write(m_fbuf.c_str(), m_fbuf.size());
	}

	const bcm2_nv_group* get_groups() const
	{
		return m_groups;
	}

	const bcm2_profile* get_profile() const
	{
		return m_profile;
	}

	const char* get_profile_name() const
	{
		return m_profile ? m_profile->name : nullptr;
	}

	private:
	string profile_id() const
	{
		return string("profile '") + m_profile->name + (m_auto_profile ? "' (auto)" : "'");
	}

	void check_file()
	{
		char md5[16];

		if (!m_profile) {
			m_profile = bcm2_profiles;
			for (; m_profile->name[0]; ++m_profile) {

				file_buf_md5(md5);

				if (!memcmp(md5, m_fbuf.c_str(), 16)) {
					m_checksum_valid = true;
					m_auto_profile = true;
					break;
				}
			}

			if (!m_profile->name[0]) {
				m_profile = nullptr;
			}
		} else {
			file_buf_md5(md5);
			m_checksum_valid = !memcmp(md5, m_fbuf.c_str(), 16);
		}

		m_encrypted = (m_fbuf.substr(16, 74) != c_header_magic);
	}

	void derive_key(const bcm2_profile *profile)
	{
		if (!profile) {
			throw user_error("password-based encryption needs a profile");
		}

		if (!profile->cfg_keyfun) {
			throw user_error(string("profile '") + m_profile->name +
					"' does not support password-based encryption");
		}

		m_key.resize(32);
		auto key = reinterpret_cast<unsigned char*>(&m_key[0]);

		if (!m_profile->cfg_keyfun(m_password.c_str(), key)) {
			throw error(string("profile ") + m_profile->name + ": cfg_keyfun failed\n");
		}
	}

	void check_header()
	{
		string actual_magic = m_dbuf.substr(0, 74);

		m_magic_valid = (actual_magic == c_header_magic);
		m_reported_size = read16(74 + 4);

		m_size_valid = false;

		if (m_dbuf.size() == m_reported_size) {
			m_size_valid = true;
		} else if (m_dbuf.size() - 16 == m_reported_size) {
			if (m_dbuf.substr(m_dbuf.size() - 16) == string(16, '\0')) {
				m_dbuf.resize(m_dbuf.size() - 16);
				m_size_valid = true;
				m_has_padding = true;
			}
		}
	}

	void file_buf_md5(char *md5) const
	{
		MD5_CTX c;
		MD5_Init(&c);
		MD5_Update(&c, &m_fbuf[16], m_fbuf.size() - 16);

		if (m_profile->cfg_md5key) {
			string key = parse_hex(m_profile->cfg_md5key);
			MD5_Update(&c, key.c_str(), key.size());
		}

		MD5_Final(reinterpret_cast<unsigned char*>(md5), &c);
	}

	bool decrypt_with_profile(const bcm2_profile* profile)
	{
		if (m_key.empty()) {

			if (!m_password.empty() && profile->cfg_keyfun) {
				derive_key(profile);
				decrypt_with_current_key();
				check_header();
				if (m_magic_valid) {
					return true;
				}
			}

			for (size_t i = 0; profile->cfg_defkeys[i][0]; ++i) {
				try {
					set_key(profile->cfg_defkeys[i], true);
				} catch (const user_error& e) {
					cerr << "warning: " << profile_id() << ": " << e.what() << endl;
					continue;
				}
				decrypt_with_current_key();
				check_header();

				if (m_magic_valid) {
					return true;
				}
			}
		}

		m_key.clear();
		return false;
	}

	void decrypt()
	{
		if (m_key.empty()) {
			// profile detection will have failed in check_header if the file was
			// incomplete. in that case, give it another go, and look for a 
			// valid header magic after encryption
			if (!m_profile) {
				m_profile = bcm2_profiles;
				for (; m_profile->name[0]; ++m_profile) {
					if (decrypt_with_profile(m_profile)) {
						break;
					}
				}

				if (!m_magic_valid) {
					m_profile = nullptr;
				}
			} else {
				decrypt_with_profile(m_profile);
			}
		} else if (!m_key.empty()) {
			decrypt_with_current_key();
		}
	}

	void decrypt_with_current_key()
	{
		m_dbuf = crypt(m_fbuf.substr(16), false);
	}

	string crypt(string src, bool encrypt)
	{
		auto key = reinterpret_cast<const unsigned char*>(m_key.c_str());
		AES_KEY aes;

		if (encrypt) {
			AES_set_encrypt_key(key, 256, &aes);
		} else {
			AES_set_decrypt_key(key, 256, &aes);
		}

		if (encrypt && m_has_padding) {
			src.append(string(16, '\0'));
		}

		string dest(src.size(), '\0');

		auto remaining = src.size();
		// look ma, no hands!
		auto oblock = reinterpret_cast<unsigned char*>(&dest[0]);
		auto iblock = reinterpret_cast<unsigned char*>(&src[0]);

		while (remaining >= 16) {
			if (encrypt) {
				AES_encrypt(iblock, oblock, &aes);
			} else {
				AES_decrypt(iblock, oblock, &aes);
			}

			remaining -= 16;
			iblock += 16;
			oblock += 16;
		}

		if (remaining) {
			memcpy(oblock, iblock, remaining);
		}

		return dest;
	}

	void parse_data()
	{
		if (m_encrypted && m_key.empty()) {
			return;
		}

		free_groups();
		auto buf = reinterpret_cast<unsigned char*>(&m_dbuf[80]);
		m_groups = bcm2_nv_parse_groups(buf, m_dbuf.size() - 80, &m_unparsed);
	}

	void free_groups()
	{
		if (m_groups) {
			bcm2_nv_free_groups(m_groups);
			m_groups = nullptr;
		}
	}

	template<typename T> T read(size_t offset)
	{
		return *(reinterpret_cast<const T*>(&m_dbuf[offset]));
	}

	template<typename T> void write(size_t offset, const T& t)
	{
		*((reinterpret_cast<T*>(&m_dbuf[offset]))) = t;
	}

	uint16_t read16(size_t offset)
	{
		return ntohs(read<uint16_t>(offset));
	}

	void write16(size_t offset, uint16_t val)
	{
		write(offset, htons(val));
	}

	const bcm2_profile *m_profile = nullptr;
	bcm2_nv_group *m_groups = nullptr;
	size_t m_unparsed;
	string m_fbuf;
	string m_dbuf;
	string m_key;
	string m_password;
	bool m_size_valid = false;
	bool m_has_padding = false;
	bool m_magic_valid = false;
	bool m_checksum_valid = false;
	bool m_auto_profile = false;
	bool m_forced_profile = false;
	bool m_encrypted = false;
	bool m_default_key = false;
	uint16_t m_reported_size = 0;
	uint8_t m_ver_maj = 0, m_ver_min = 0;

	static constexpr const char* c_header_magic =
		"6u9E9eWF0bt9Y8Rw690Le4669JYe4d-056T9p"
		"4ijm4EA6u9ee659jn9E-54e4j6rPj069K-670";
	static constexpr uint16_t c_data_offset = 16 + 74 + 4 + 2;
};

[[noreturn]] void usage_and_exit(int exitstatus)
{
	fprintf(stderr,
			"Usage: bcm2cfg [options]\n"
			"\n"
			"Commands:\n"
			"  ver             Verify input file\n"
			"  fix             Fix checksum and file size\n"
			"  dec             Decrypt input file\n"
			"  enc             Encrypt input file\n"
			"  show            Dump contents\n"
			"  info            Show terse info\n"
			"\n"
			"Options:\n"
			"  -h              Show help\n"
			"  -p <password>   Backup password\n"
			"  -k <key>        Backup key\n"
			"  -o <output>     Output file\n"
			"  -n              Ignore bad checksum\n"
			"  -L              List profiles\n"
			"  -P <profile>    Select device profile\n"
			"  -O <var>=<arg>  Override profile variable\n"
			"  -v              Verbose operation\n"
			"  -z              Pad before encrypting\n"
			"\n"
			"bcm2cfg " VERSION " Copyright (C) 2016 Joseph C. Lehner\n"
			"Licensed under the GNU GPLv3; source code is available at\n"
			"https://github.com/jclehner/bcm2utils\n"
			"\n");
	exit(exitstatus);
}

char *magic_to_str(const bcm2_nv_group_magic *m)
{
    static char str[32];
    unsigned k = 0;

    str[0] = '\0';

    for (; k < 2; ++k) {
        unsigned i = 0;
        for (; i < 4; ++i) {
            char c = m->s[i];
            sprintf(str, k ? "%s%c" : "%s%02x", str, k ? (isprint(c) ? c : ' ') : c);
        }
        sprintf(str, "%s ", str);
    }

    return str;
}

string version_to_str(const uint8_t *version)
{
	return to_string(version[0]) + "." + to_string(version[1]);
}

void dump_header(const settings& gws)
{
	cout << "size        " << gws.get_file_size() << " bytes" << endl;
	cout << "profile     ";

	if (!gws.has_forced_profile()) {
		if (!gws.has_auto_profile()) {
			cout << "unknown" << endl;
		} else {
			cout << gws.get_profile_name() << " (auto-detected)" << endl;
		}
	} else {
		cout << gws.get_profile_name() << " (forced)" << endl;
	}

	cout << "checksum    " << gws.get_checksum();

	if (gws.get_profile()) {
		cout << " " << (gws.is_checksum_valid() ? "(ok)" : "(bad)");
	}

	cout << endl;

	cout << "key         ";
	if (!gws.is_encrypted()) {
		cout << "(none)" << endl;
	} else {
		if (gws.is_magic_valid()) {
			cout << gws.get_key() << endl;
		} else {
			cout << "(decryption failed)" << endl;
		}
	}

	if (gws.is_magic_valid()) {
		cout << "data        " << gws.get_reported_size() << " bytes " <<
			(gws.is_size_valid() ? "(ok)" : "(bad)") << endl;

		cout << "padded      " << (gws.has_padding() ? "yes" : "no") << endl;
		cout << endl;
	}

}

void dump_settings(const settings& gws)
{
	dump_header(gws);

	if (gws.is_magic_valid()) {

		cout << "offset--id-------------type-------------------------------------version-----size" << endl;

		const bcm2_nv_group *group = gws.get_groups();
		for (; group; group = group->next) {
			printf(" %5zu  %s %-40s %-5s    %5u b", gws.get_data_offset() + group->offset,
					magic_to_str(&group->magic), group->name,
					version_to_str(group->version).c_str(), group->size);
			if (group->invalid) {
				printf(" (invalid)");
			}
			printf("\n");
		}
	}

}
}

int do_main(int argc, char **argv)
{
	ios_base::sync_with_stdio();

	string cmd;
	if (argc != 1 && argv[1][0] != '-') {
		cmd = argv[1];
		argv += 1;
		argc -= 1;
	}

	settings gws;

	string infile, outfile;
	bcm2_profile *profile = NULL;
	bool noverify = false;
	int verbosity = 0;

	int c;

	while ((c = getopt(argc, argv, "p:k:i:o:P:O:Lvzn")) != -1) {
		switch (c) {
			case 'n':
				noverify = true;
				break;
			case 'p':
				if (gws.has_key()) {
					cerr << "error: -p and -k are mutually exclusive" << endl;
					return 1;
				}
				gws.set_password(optarg);
				break;
			case 'k':
				if (gws.has_password()) {
					cerr << "error: -p and -k are mutually exclusive" << endl;
					return 1;
				}
				gws.set_key(optarg, true);
				break;
			case 'i':
				infile = optarg;
				break;
			case 'o':
				outfile = optarg;
				break;
			case 'L':
			case 'O':
			case 'P':
			case 'v':
				if (!handle_common_opt(c, optarg, &verbosity, &profile)) {
					return 1;
				}

				if (profile) {
					gws.set_profile(profile);
				}

				break;
			case 'z':
				gws.set_padding();
				break;
			case 'h':
				usage_and_exit(0);
				break;
			default:
				usage_and_exit(1);
		}
	}

	if (cmd.empty()) {
		usage_and_exit(1);
	}

	if (infile.empty()) {
		cerr << "error: no input file specified" << endl;
		return 1;
	}

	if (cmd == "ver" && noverify) {
		cerr << "error: -n cannot be used with 'ver' command" << endl;
		return 1;
	}

	gws.set_profile(profile);
	gws.read(infile);

	if (cmd == "info") {
		if (gws.has_auto_profile()) {
			cout << infile << " ";
			cout << gws.get_profile_name() << " ";

			cout << (gws.is_checksum_valid() ? '+' : '-') << "chk ";
			cout << (gws.is_magic_valid() ? '+' : '-') << "magic ";
			cout << (gws.is_size_valid() ? '+' : '-') << "size ";
			cout << (gws.is_encrypted() ? '+' : '-') << "enc ";

			cout << endl;
		}

		return 0;
	}

	cout << "FILE: " << infile << endl;
	cout << "============================================" << endl;

	if (cmd == "show") {
		dump_settings(gws);
	} else {
		dump_header(gws);
	}

	if (cmd == "ver") {
		bool bad = false;

		if (!gws.is_checksum_valid()) {
			cout << "failed: checksum" << endl;
			bad = true;
		}

		if (!gws.is_encrypted() || !gws.get_key().empty()) {
			if (!gws.is_magic_valid()) {
				cout << "failed: magic" << endl;
				bad = true;
			}

			if (!gws.is_size_valid()) {
				cout << "failed: size" << endl;
				bad = true;
			}
		}

		if (bad) {
			return 1;
		}

		cout << "verification passed" << endl;
	} else if (cmd == "fix") {

		if (outfile.empty()) {
			outfile = infile;
		}

		gws.write(outfile);
		cout << "writing fixed file to " << outfile << endl;
	} else if (cmd == "dec" || cmd == "enc") {
#if 0
		if (!gws.has_pw_or_key()) {
			cerr << "error: no key or password specified" << endl;
			return 1;
		}
#endif

		if (cmd == "dec" && !gws.is_encrypted()) {
			cout << "file is not encrypted; nothing to do" << endl;
			return 0;
		} else if (cmd == "enc" && gws.is_encrypted()) {
			cout << "file is already encrypted" << endl;
			return 0;
		}

		if (cmd == "enc" && !gws.has_pw_or_key()) {
			if (!gws.get_profile()) {
				cerr << "error: must specify profile" << endl;
				return 1;
			}

			const char *defkey = gws.get_profile()->cfg_defkeys[0];
			if (defkey && defkey[0]) {
				gws.set_key(defkey, true);
			}
		}

		gws.write(outfile, cmd == "enc");
		// yes, this is ugly.
		cout << "writing " << cmd << "rypted file to " << outfile << endl;
	}

	return 0;
}

int main(int argc, char **argv)
{
	try {
		return do_main(argc, argv);
	} catch (const user_error& e) {
		cerr << "error: " << e.what() << endl;
	} catch (const error& e) {
		cerr << "error: " << e.what() << endl;
	} catch (const exception& e) {
		cerr << "error: " << typeid(e).name() << ": " << e.what() << endl;
	} catch (...) {
		cerr << "error: caught unknown exception" << endl;
	}

	return 1;
}


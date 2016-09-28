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

#include <iostream>
#include <cstring>
#include <set>
#include "profile.h"
#include "util.h"

using namespace std;

namespace bcm2dump {
namespace {

string from_hex(const string& hex)
{
	if (hex.empty()) {
		return "";
	}

	if (hex.size() % 2 == 0) {
		string buf;
		for (string::size_type i = 0; i < hex.size(); i += 2) {
			try {
				buf += lexical_cast<int>(hex.substr(i, 2), 16);
			} catch (const bad_lexical_cast& e) {
				break;
			}
		}

		if (buf.size() == hex.size() / 2) {
			return buf;
		}
	}

	throw invalid_argument("invalid hex string: " + hex);
}

string rpad(string str, size_t padding)
{
	return str.size() >= padding ? str : str + string(padding - str.size(), ' ');
}

string lpad(string str, size_t padding)
{
	return str.size() >= padding ? str : string(padding - str.size(), ' ') + str;
}

string row(const string& name, size_t padding, const string& value)
{
	return rpad(name, padding) + "  " + value;
}

template<class T> string row(const string& name, size_t padding, const T& value)
{
	return row(name, padding, to_string(value));
}

string to_pretty_size(size_t size)
{
	if (!(size % (1024 * 1024))) {
		return to_string(size / (1024 * 1024)) + " MB";
	} else if (!(size % 1024)) {
		return to_string(size / 1024) + " KB";
	} else {
		return to_string(size) + "  B";
	}
}

class profile_wrapper : public profile
{
	public:
	profile_wrapper(const bcm2_profile* p)
	: m_p(p)
	{
		parse_spaces();
		parse_codecfg();
		parse_magic();
		parse_keys();
	}

	virtual ~profile_wrapper() {}

	virtual string name() const override
	{ return m_p->name; }

	virtual string pretty() const override
	{ return m_p->pretty; }

	virtual bool mipsel() const override
	{ return m_p->mipsel; }

	virtual uint32_t kseg1() const override
	{ return m_p->kseg1mask; }

	virtual unsigned baudrate() const override
	{ return m_p->baudrate; }

	virtual uint16_t pssig() const override
	{ return m_p->pssig; }

	virtual uint16_t blsig() const override
	{ return m_p->blsig; }

	virtual vector<const bcm2_magic*> magics() const override
	{ return m_magic; }

	virtual vector<addrspace> spaces() const override
	{ return m_spaces; }

	virtual const addrspace& space(const string& name, bcm2_interface intf) const override
	{
		if (name == "ram") {
			return ram();
		}

		for (const addrspace& s : m_spaces) {
			if (s.name() == name) {
				return s;
			}
		}

		throw invalid_argument(this->name() + ": no such address space: " + name);
	}

	virtual const addrspace& ram() const override
	{ return m_ram; }

	virtual const codecfg_type& codecfg(bcm2_interface intf) const override
	{ return m_codecfg; }

	virtual string md5_key() const override
	{ return from_hex(m_p->cfg_md5key); }

	virtual vector<string> default_keys() const override
	{ return m_keys; }

	virtual size_t key_length() const override
	{ return 32; }

	virtual string derive_key(const string& pw) const override
	{
		if (!m_p->cfg_keyfun) {
			throw runtime_error(name() + ": password-based encryption is not supported");
		}

		unsigned char key[32];
		if (!m_p->cfg_keyfun(pw.c_str(), key)) {
			throw runtime_error(name() + ": key derivation failed");
		}

		return string(reinterpret_cast<char*>(key), 32);
	}

	private:
	void check_range(uint32_t addr, uint32_t offset, const string& name) const
	{
		m_ram.check_range(addr, offset, name);
	}

	uint32_t check_addr(uint32_t addr, const string& name) const
	{
		return m_ram.check_offset(addr, name);
	}

	void parse_spaces()
	{
		const bcm2_addrspace* s = m_p->spaces;
		for (; s->name[0]; ++s) {
			m_spaces.push_back(addrspace(s, *this));
			if (m_spaces.back().name() == "ram") {
				m_ram = m_spaces.back();
			}
		}

		if (m_ram.name().empty()) {
			throw runtime_error(name() + ": no 'ram' address space defined");
		}
	}

	void parse_codecfg()
	{
		m_codecfg.loadaddr = check_addr(m_p->loadaddr, "loadaddr");
		m_codecfg.buffer = check_addr(m_p->buffer, "buffer");

		if (m_codecfg.buffer) {
			if (!m_p->buflen) {
				if (m_p->loadaddr > m_p->buffer) {
					m_codecfg.buflen = m_p->loadaddr - m_p->buffer;
				} else if (m_ram.size()) {
					m_codecfg.buflen = m_p->buffer - m_ram.size();
				} else {
					m_codecfg.buflen = 0;
				}
			} else {
				m_codecfg.buflen = m_p->buflen;
			}

			if (m_codecfg.buflen) {
				m_ram.check_range(m_codecfg.buffer, m_codecfg.buflen, "buffer");
			}
		}

		m_codecfg.printf = check_addr(m_p->printf, "printf");
		m_codecfg.sscanf = check_addr(m_p->sscanf, "sscanf");
		m_codecfg.fgets = check_addr(m_p->fgets, "fgets");
		m_codecfg.scanf = check_addr(m_p->scanf, "scanf");
	}

	void parse_magic()
	{
		for (size_t i = 0; i < BCM2_INTF_NUM && m_p->magic[i].addr; ++i) {
			const bcm2_magic* m = &m_p->magic[i];
			m_ram.check_range(m->addr, strlen(m->data), "magic");
			m_magic.push_back(m);
		}
	}

	void parse_keys()
	{
		const char* key = nullptr;

		for (size_t i = 0; (key = m_p->cfg_defkeys[i]) && key[i]; ++i) {
			size_t length = strlen(key);
			if (length == key_length()) {
				m_keys.push_back(key);
			} else if ((length / 2) == key_length()) {
				m_keys.push_back(from_hex(key));
			} else {
				throw invalid_argument(name() + ": key does not match key length: " + key);
			}
		}
	}

	const bcm2_profile* m_p = nullptr;
	vector<string> m_keys;
	vector<const bcm2_magic*> m_magic;
	vector<addrspace> m_spaces;
	addrspace m_ram;
	codecfg_type m_codecfg;
};
}

addrspace::addrspace(const bcm2_addrspace* a, const profile& p)
: m_p(a), m_profile_name(p.name())
{
	m_kseg1 = (name() == "ram") ? p.kseg1() : 0;

	if (m_p->size) {
		m_size = m_p->size;
		if (m_kseg1 && (m_p->min + m_size) > (m_p->min | m_kseg1)) {
			throw invalid_argument(p.name() + ": " + name() 
					+ ": size extends into kseg1");
		}
	} else {
		m_size = (m_kseg1 ? ((m_p->min | m_kseg1) - 1) : 0xffffffff) - m_p->min + 1;
	}

	set<string> names;
	const bcm2_partition* part = m_p->parts;
	for (; part->name[0]; ++part) {
		if (!names.insert(part->name).second) {
			throw invalid_argument(p.name() + ": " + name() + ": non-unique "
					+ "partition name " + part->name);
		}

		check_range(part->offset, part->size, string("partition ") + part->name);
		m_partitions.push_back(part);
	}

	const bcm2_func* read = m_p->read;
	for (; read->addr; ++read) {
		for (auto f : m_read_funcs) {
			if (f.intf() & read->intf) {
				throw invalid_argument(p.name() + ": " + name() + " function "
						+ "0x" + to_hex(read->addr) + " conflicts with 0x" + to_hex(f.addr()));
			}
		}

		p.ram().check_offset(read->addr, "function 'read'");
		m_read_funcs.push_back(func(read->addr, read->mode, read->intf, read->retv));

		for (size_t i = 0; i < BCM2_PATCH_NUM; ++i) {
			p.ram().check_offset(read->patch[i].addr, "function 'read', patch " + to_string(i));
			m_read_funcs.back().patches().push_back(&read->patch[i]);
		}
	}
}

bool addrspace::check_range(uint32_t offset, uint32_t length, const string& name, bool exception) const
{
	// ignore memory address of 0
	if (!offset && is_ram()) {
		return true;
	} else if (!min() && !size()) {
		return true;
	}

	if (!(offset % alignment())) {
		uint32_t offset_c = offset & ~m_kseg1;
		uint32_t last = offset_c + length - 1;
		uint32_t max = min() + m_size - 1;
		if (offset_c >= min() && m_size && offset_c <= max) {
			if (!m_size || !length || last <= max) {
				return true;
			}
		}

		if (!exception) {
			return false;
		}
	}

	string msg;

	if (length) {
		msg = "range " + this->name() + ":0x" + to_hex(offset) + "-0x" + to_hex(offset + length - 1);
	} else {
		msg = "offset " + this->name() + ":0x" + to_hex(offset);
	}

	throw user_error(m_profile_name + ": invalid " + msg + (!name.empty() ? ("(" + name + ")") : ""));
}

const addrspace::part& addrspace::partition(const string& name) const
{
	for (const part& p : m_partitions) {
		if (p.name() == name) {
			return p;
		}
	}

	throw user_error(m_profile_name + ": " + this->name() + ": no such partition: " + name);
}

func addrspace::get_read_func(bcm2_interface intf) const
{
	for (auto f : m_read_funcs) {
		if (f.intf() & intf) {
			return f;
		}
	}

	return func();
}

vector<profile::sp> profile::s_profiles;

const profile::sp& profile::get(const string& name)
{
	for (const profile::sp& p : list()) {
		if (!strcasecmp(p->name().c_str(), name.c_str())) {
			return p;
		}
	}

	throw user_error("no such profile: " + name);
}

const vector<profile::sp>& profile::list()
{
	if (s_profiles.empty()) {
		for (const bcm2_profile* p = bcm2_profiles; p->name[0]; ++p) {
			s_profiles.push_back(make_shared<profile_wrapper>(p));
		}
	}

	return s_profiles;
}

void profile::print_to_stdout(bool verbose) const
{
	cout << name() << ": " << pretty() << endl;
	cout << string(name().size() + 2 + pretty().size(), '=') << endl;

	//cout << row("baudrate", 12, baudrate()) << endl;
	cout << row("pssig", 12, "0x" + to_hex(pssig())) << endl;
	cout << row("blsig", 12, "0x" + to_hex(blsig())) << endl;

	for (auto space : spaces()) {
		cout << endl << rpad(space.name(), 12) << "  0x" << to_hex(space.min());
		if (space.size()) {
			cout << " - 0x" << to_hex(space.min() + space.size() - 1);
			cout << "  (" << lpad(to_pretty_size(space.size()), 9) << ")  ";
		} else {
			cout << string(28, ' ');
		}

		if (space.is_ram() || space.is_writable()) {
			cout << "RW";
		} else {
			cout << "RO";
		}

		cout << endl << string(54, '-') << endl;

		if (space.partitions().empty()) {
			cout << "(no partitions defined)" << endl;
		}

		for (auto part : space.partitions()) {
				cout << rpad(part.name(), 12) << "  0x" << to_hex(part.offset());
				if (part.size()) {
					cout << " - 0x" << to_hex(part.offset() + part.size() - 1) << "  ("
							<< lpad(to_pretty_size(part.size()), 9) << ")";
				}
				cout << endl;
		}
	}
}
}

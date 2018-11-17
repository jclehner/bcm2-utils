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

template<class T> constexpr size_t array_size(const T array)
{
	return sizeof(array) / sizeof(array[0]);
}

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

template<class T> void set_if_unset(T& dest, const T& src)
{
	if (!dest) {
		dest = src;
	}
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
		//parse_codecfg();
		parse_magic();
		parse_keys();
		parse_versions();
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

	virtual vector<version> versions() const override
	{ return m_versions; }

	virtual const version& default_version(int intf) const override
	{
		static version dummy;
		auto iter = m_defaults.find(intf);
		return iter != m_defaults.end() ? iter->second : dummy;
	}

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

	virtual uint32_t cfg_encryption() const override
	{ return cfg_flags() & BCM2_CFG_ENC_MASK; }

	virtual uint32_t cfg_flags() const override
	{ return m_p->cfg_flags; }

	virtual vector<string> default_keys() const override
	{ return m_keys; }

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

#if 0
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
		m_codecfg.getline = check_addr(m_p->getline, "getline");
		m_codecfg.scanf = check_addr(m_p->scanf, "scanf");
	}
#endif

	void parse_magic()
	{
		for (size_t i = 0; i < BCM2_INTF_NUM && m_p->magic[i].addr; ++i) {
			const bcm2_magic* m = &m_p->magic[i];
			m_ram.check_range(m->addr, magic_size(m), "magic");
			m_magic.push_back(m);
		}
	}

	void parse_keys()
	{
		const char* key = nullptr;

		for (size_t i = 0; (key = m_p->cfg_defkeys[i]) && key[i]; ++i) {
			m_keys.push_back(from_hex(key));
		}
	}

	void parse_versions()
	{
		const bcm2_version* v = m_p->versions;

		for (size_t i = 0; i < ARRAY_SIZE(m_p->versions); ++i) {
			if (!v[i].version[0]) {
				m_defaults[v[i].intf] = version(&v[i], this, nullptr);
			}
		}

		for (size_t i = 0; i < ARRAY_SIZE(m_p->versions); ++i) {
			if (!v[i].version[0]) {
				continue;
			}

			m_versions.push_back(version(&v[i], this, m_defaults[v[i].intf].raw()));
		}
	}

	const bcm2_profile* m_p = nullptr;
	vector<string> m_keys;
	vector<const bcm2_magic*> m_magic;
	vector<version> m_versions;
	map<int, version> m_defaults;
	vector<addrspace> m_spaces;
	addrspace m_ram;
	codecfg_type m_codecfg;
};

func get_func(const vector<func>& funcs, int intf)
{
	for (auto f : funcs) {
		if (f.intf() & intf) {
			return f;
		}
	}

	return func();
}

void parse_funcs(const addrspace& a, const profile& p, const bcm2_func* ifuncs, vector<func>& ofuncs)
{
	for (auto ifunc = ifuncs; ifunc->addr; ++ifunc) {
		func f = get_func(ofuncs, ifunc->intf);
		if (f.addr()) {
			throw invalid_argument(p.name() + ": " + a.name() + " function "
					+ "0x" + to_hex(ifunc->addr) + " conflicts with 0x" + to_hex(f.addr()));
		}

		p.ram().check_offset(ifunc->addr, "function");
		ofuncs.push_back(func(ifunc->addr, ifunc->mode, ifunc->intf, ifunc->retv));

		for (size_t i = 0; i < BCM2_PATCH_NUM; ++i) {
			p.ram().check_offset(ifunc->patch[i].addr, "function 0x" + to_hex(ifunc->addr) + " patch " + to_string(i));
			ofuncs.back().patches().push_back(&ifunc->patch[i]);
		}
	}
}

const bcm2_typed_val* get_version_opt(const bcm2_version* v, const string& name, bcm2_type type)
{
	for (auto i = 0; i < ARRAY_SIZE(v->options); ++i) {
		if (v->options[i].name == name) {
			if (type != BCM2_TYPE_NIL && type != v->options[i].type) {
				throw runtime_error(name + ": invalid type requested");
			}
			return &v->options[i];
		}
	}

	if (type == BCM2_TYPE_NIL) {
		return nullptr;
	}

	throw runtime_error(name + ": no such option");
}
}

void version::parse_codecfg()
{
	auto ram = m_prof->ram();

#define PARSE_ADDR(x) m_codecfg[#x] = ram.check_offset(m_p->x ? m_p->x : m_def->x, "function " #x)
	PARSE_ADDR(printf);
	PARSE_ADDR(sscanf);
	PARSE_ADDR(scanf);
	PARSE_ADDR(getline);
	PARSE_ADDR(buffer);
	PARSE_ADDR(loadaddr);
#undef PARSE_ADDR

	if ((m_codecfg["buflen"] = m_p->buflen ? m_p->buflen : m_def->buflen)) {
		auto buffer = m_codecfg["buffer"];
		ram.check_range(buffer, buffer + m_codecfg["buflen"]);
	}
}

void version::parse_functions()
{
	for (size_t i = 0; i < ARRAY_SIZE(m_p->spaces); ++i) {
		const bcm2_version_addrspace* s = m_p->spaces + i;
		if (!s->name[0]) {
			break;
		}

		// this throws if the address space does not exist
		m_prof->space(s->name, static_cast<bcm2_interface>(m_p->intf));
#define PARSE_ADDR(x) \
do { \
	auto addr = m_prof->ram().check_offset(s->x.addr, "function " #x); \
	m_functions[s->name][#x] = func(addr, s->x.mode, m_p->intf, s->x.retv); \
	for (size_t k = 0; k < ARRAY_SIZE(s->x.patch); ++k) { \
		m_prof->ram().check_offset(s->x.patch[k].addr, "patch " #x); \
		m_functions[s->name][#x].patches().push_back(&s->x.patch[k]); \
	} \
} while(0)

		PARSE_ADDR(open);
		PARSE_ADDR(read);
		PARSE_ADDR(write);
		PARSE_ADDR(erase);
		PARSE_ADDR(close);
#undef PARSE_ADDR
	}
}

const bcm2_typed_val* version::get_opt(const string& name, bcm2_type type) const
{
	auto ret = get_version_opt(m_p, name, BCM2_TYPE_NIL);
	if (ret && (ret->type == type || type == BCM2_TYPE_NIL)) {
		return ret;
	}

	return get_version_opt(m_def, name, type);
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

	parse_funcs(*this, p, m_p->read, m_read_funcs);
	parse_funcs(*this, p, m_p->write, m_write_funcs);
	parse_funcs(*this, p, m_p->erase, m_erase_funcs);
}

bool addrspace::check_range(uint32_t offset, uint32_t length, const string& name, bool exception) const
{
	// ignore memory address of 0
	if (!offset && is_ram()) {
		return true;
	} else if (!min() && !size()) {
		return true;
	}

	string msg;

	if (!(offset % alignment())) {
		uint32_t offset_c = offset & ~m_kseg1;
		uint32_t last = offset_c + length - 1;
		uint32_t max = min() + m_size - 1;
		if (offset_c >= min() && m_size && offset_c <= max) {
			if (!m_size || !length || last <= max) {
				return true;
			}
		}

		if (length) {
			msg = "invalid range " + this->name() + ":0x" + to_hex(offset)
				+ "-0x" + to_hex(offset + length - 1);
		} else {
			msg = "invalid offset " + this->name() + ":0x" + to_hex(offset);
		}
	} else {
		msg = "unaligned offset " + this->name() + ":0x" + to_hex(offset);
	}

	if (!exception) {
		return false;
	}

	throw user_error(m_profile_name + ": " + msg + (!name.empty() ? ("(" + name + ")") : ""));
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

const addrspace::part& addrspace::partition(uint32_t offset) const
{
	for (const part& p : m_partitions) {
		if (p.offset() == offset) {
			return p;
		}
	}

	throw user_error(m_profile_name + ": no partition at offset " + to_string(offset));
}

func addrspace::get_read_func(bcm2_interface intf) const
{ return get_func(m_read_funcs, intf); }

func addrspace::get_write_func(bcm2_interface intf) const
{ return get_func(m_write_funcs, intf); }

func addrspace::get_erase_func(bcm2_interface intf) const
{ return get_func(m_erase_funcs, intf); }

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

uint32_t magic_size(const bcm2_magic* magic)
{
	if (!magic->size) {
		return strnlen(magic->data, sizeof(bcm2_magic::data));
	} else {
		return min(sizeof(bcm2_magic::data), size_t(magic->size));
	}
}

string magic_data(const bcm2_magic* magic)
{
	return string(magic->data, magic_size(magic));
}

string get_profile_names(unsigned width, unsigned indent)
{
	string names, indstr;
	size_t w = 0;

	if (indent) {
		indstr = string(indent, ' ');
		names += indstr;
	}

	for (auto p : profile::list()) {
		string n = p->name();

		if ((w + indent + n.size() + 2) > width) {
			names += "\n" + indstr;
			w = 0;
		}

		names += n + ", ";
		w += n.size() + 2;
	}

	return names.substr(0, names.size() - 2);
}
}

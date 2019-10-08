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

#include <iostream>
#include <string>
#include <set>
#include "nonvol2.h"
#include "util.h"
using namespace std;
using namespace bcm2dump;

namespace bcm2cfg {
namespace {
std::string desc(const nv_val::named& var)
{
	return var.name + " (" + var.val->type() + ")";
}

template<typename T> T read_num(istream& is)
{
	T num;
	is.read(reinterpret_cast<char*>(&num), sizeof(T));
	return ntoh(num);
}

template<typename T> void write_num(ostream& os, T num)
{
	num = hton(num);
	os.write(reinterpret_cast<const char*>(&num), sizeof(T));
}

string pad(unsigned level)
{
	return string(2 * (level + 1), ' ');
}

size_t to_index(const string& str, const nv_val& val)
{
	try {
		size_t i = lexical_cast<size_t>(str);
		if (i >= val.bytes()) {
			throw runtime_error("index " + str + " invalid for " + val.type());
		}
		return i;
	} catch (const bad_lexical_cast& e) {
		throw runtime_error("invalid index " + str);
	}
}

bool read_group_header(istream& is, nv_u16& size, nv_magic& magic, size_t remaining)
{
	if (size.read(is)) {
		if (size.num() < 6) {
			logger::v() << "group size " << size.to_str() << " too small to be valid" << endl;
			return false;
		} else if (!magic.read(is)) {
			logger::v() << "failed to read group magic" << endl;
			return false;
		}

		return true;
	} else {
		logger::v() << "failed to read group size" << endl;
	}

	return false;
}

string data_to_string(const string& data, unsigned level, bool pretty)
{
	const unsigned threshold = 24;
	ostringstream ostr;
	bool multiline = /*pretty && */(data.size() > threshold);
	if (multiline) {
		ostr << "{";
	}

	for (size_t i = 0; i < data.size(); ++i) {
		if (!(i % threshold)) {
			if (multiline) {
				ostr << endl << pad(level) << "0x" << to_hex(i, 3) << " = ";
			}
		} else {
			ostr << ':';
		}

		ostr << setw(2) << setfill('0') << hex << uppercase << (data[i] & 0xff);
	}

	if (multiline) {
		ostr << endl << pad(level - 1) + "}";
	}

	return ostr.str();
}

string compound_to_string(const nv_compound& c, unsigned level, bool pretty,
		const string& name = "", const nv_array_base::is_end_func& is_end = nullptr)
{
	string str = "{";
	size_t i = 0;

	auto parts = c.parts();
	for (; i < parts.size(); ++i) {
		auto v = parts[i];
		if (is_end && is_end(v.val)) {
			break;
		} else if (v.val->is_disabled() || (pretty && v.name[0] == '_' && false)) {
			continue;
		} else if (pretty && (!v.val->is_set() || v.name[0] == '_')) {
			continue;
		}

		str += "\n" + pad(level) + v.name + " = ";
		if (v.val->is_set()) {
			str += v.val->to_string(level + 1, pretty);
		} else {
			str += "<n/a>";
		}
	}

	if (i != parts.size() && is_end) {
		str += "\n" + pad(level) + std::to_string(i) + ".." + std::to_string(parts.size() - 1) + " = <n/a>";
	}

	str += "\n" + pad(level - 1) + "}";

	return str;

}

string magic_to_string(const string& buf, bool pretty, char filler)
{
	string str;
	if (pretty) {
		for (auto c : buf) {
			if (isalnum(c)) {
				str += c;
			} else if (filler) {
				str += filler;
			}
		}

		if (str.size() >= 2) {
			return str;
		}
	}

	return to_hex(buf);
}

size_t compound_size(const nv_compound& c)
{
	size_t size = 0;

	for (auto p : c.parts()) {
		if (p.val->is_compound()) {
			size += compound_size(*nv_val_cast<nv_compound>(p.val));
		} else {
			size += p.val->bytes();
		}
	}

	return size;
}

size_t str_prefix_max(int flags)
{
	if (flags & nv_string::flag_prefix_u8) {
		return 0xff;
	} else if (flags & nv_string::flag_prefix_u16) {
		return 0xffff;
	}

	return string::npos -1;
}

size_t str_prefix_bytes(int flags)
{
	if (flags & nv_string::flag_prefix_u8) {
		return 1;
	} else if (flags & nv_string::flag_prefix_u16) {
		return 2;
	}

	return 0;
}

size_t str_extra_bytes(int flags)
{
	return flags & nv_string::flag_require_nul ? 1 : 0;
}

size_t str_max_length(int flags, size_t width)
{
	if (width) {
		return width - str_extra_bytes(flags);
	}

	size_t max = str_prefix_max(flags);
	if (flags & nv_string::flag_size_includes_prefix) {
		max -= str_prefix_bytes(flags);
	}

	return max - str_extra_bytes(flags);

}

bool is_valid_identifier(const std::string& name)
{
	if (name.empty()) {
		return false;
	}

	for (char c : name) {
		if (!isalnum(c) && c != '-' && c != '_') {
			return false;
		}
	}

	return true;
}
}

csp<nv_val> nv_val::get(const string& name) const
{
	throw runtime_error("requested member '" + name + "' of non-compound type " + type());
}

void nv_val::set(const string& name, const string& val)
{
	throw runtime_error("requested member '" + name + "' of non-compound type " + type());
}

nv_val& nv_val::parse_checked(const std::string& str)
{
	if (!parse(str)) {
		throw runtime_error("conversion to " + type() + " failed: '" + str + "'");
	}

	return *this;
}

bool nv_compound::parse(const string& str)
{
	throw invalid_argument("cannot directly set value of compound type " + type());
}

csp<nv_val> nv_compound::get(const string& name) const
{
	auto val = find(name);
	if (!val) {
		throw invalid_argument("requested non-existing member '" + name + "'");
	}

	return val;
}

void nv_compound::set(const string& name, const string& val)
{
	auto parts = split(name, '.', false, 2);
	if (parts.size() == 2) {
		const_pointer_cast<nv_val>(get(parts[0]))->set(parts[1], val);
		return;
	}

	sp<nv_val> v = const_pointer_cast<nv_val>(get(name));
	if (!v->is_set()) {
		const nv_compound* parent = v->parent();
		while (parent && !parent->is_set()) {
			parent = parent->parent();
		}

		string preceding_unset_name;

		if (parent) {
			for (auto p : parent->parts()) {
				if (!p.val->is_disabled()) {
					if (p.name == name || ends_with(name, "." + p.name)) {
						break;
					}

					if (!p.val->is_set()) {
						throw user_error("cannot set '" + name + 
								"' without setting '" + p.name + "' first");
					}
				}
			}
		}
	}

	ssize_t diff = v->is_set() ? v->bytes() : 0;
	logger::t() << type() << ": set " << name << ": size change " << diff << " -> ";
	diff -= v->parse_checked(val).bytes();
	logger::t() << v->bytes();
	v->parent(this);
	logger::t() << ", group size " << m_bytes << " -> " << (m_bytes - diff) << " (" << diff << ")" << endl;
	m_bytes -= diff;
}

csp<nv_val> nv_compound::find(const string& name) const
{
	vector<string> tok = split(name, '.', false, 2);

	for (auto c : parts()) {
		if (c.val->is_disabled()) {
			continue;
		} else if (c.name == tok[0]) {
			if (tok.size() == 2) {
				if (c.val->is_compound()) {
					return nv_val_cast<nv_compound>(c.val)->find(tok[1]);
				} else {
					break;
				}
			} else {
				return c.val;
			}
		}
	}

	return nullptr;
}

bool nv_compound::init(bool force)
{
	if (m_parts.empty() || force) {
		m_parts = definition();
		for (auto part : m_parts) {
			part.val->parent(this);
		}
		//m_bytes = 0;
		m_set = false;
		return true;
	}

	return false;
}

istream& nv_compound::read(istream& is)
{
	clear();

	std::set<string> names;
	unsigned unk = 0;

	// do this for all parts, regardless of whether they
	// are found in the config file
	for (auto& v : m_parts) {
		if (v.name.empty()) {
			v.name = "_unk_" + std::to_string(++unk);
		}
	}

	for (auto& v : m_parts) {
		if (!names.insert(v.name).second) {
			throw runtime_error("redefinition of member " + v.name);
		} else if (!is_valid_identifier(v.name)) {
			throw runtime_error("invalid identifier name " + v.name);
		} else if (v.val->is_disabled()) {
			logger::t() << "skipping disabled " << desc(v) << endl;
			continue;
		}

		bool end = false;

		logger::t() << "pos " << is.tellg() << ": " << desc(v) << " " << v.val->bytes() << endl;

		if ((m_width && (m_bytes + v.val->bytes()) > m_width)) {
			throw runtime_error(v.name + ": variable size exceeds compound size");
		}

		auto pos = is.tellg();

		if (!end) {
			v.val->read(is);
			logger::t() << " = " << v.val->to_string(0, false) << endl;
		}

		if (!is) {
			if (!is.eof()) {
				throw runtime_error(type() + ": read error");
			}

			logger::t() << "  encountered eof while reading" << endl;
			end = true;
		}

		// check again, because a successful read may have changed the
		// byte count (by a pX_string for instance)

		if (!end && (m_width && (m_bytes + v.val->bytes()) > m_width)) {
			throw runtime_error("new variable size exceeds compound size");
		}

		if (end) {
			if (!m_partial) {
				throw runtime_error(type() + ": unexpected end of data");
			}

			// seek to the end of this compound, so we can try to continue parsing

			if (!is.eof()) {
				is.clear();
				is.seekg(pos);
				is.seekg(m_width - m_bytes, ios::cur);
				is.clear(ios::failbit);
			}

			break;
		}

		m_bytes += v.val->bytes();
		m_set = true;

		if (m_width && (m_width == m_bytes)) {
			break;
		}
	}

	return is;
}

ostream& nv_compound::write(ostream& os) const
{
	if (parts().empty()) {
		throw runtime_error("attempted to serialize uninitialized compound " + name() + " " + type());
	}

	size_t pos = 0;

	for (auto v : parts()) {
		logger::t() << "pos " << pos << ": ";
		if (v.val->is_disabled()) {
			logger::t() << v.name << " (disabled)" << endl;
			continue;
		} else if (!v.val->is_set()) {
			if (m_partial) {
				logger::t() << v.name << " (unset)" << endl;
				continue;
			}
			logger::t() << "writing unset " << name() << "." << v.name << endl;
		}

		if (!v.val->write(os)) {
			throw runtime_error("failed to write " + desc(v));
		}

		logger::t() << desc(v) << endl;
		pos += v.val->bytes();
	}

	return os;
}

std::string nv_compound::to_string(unsigned level, bool pretty) const
{
	if (!pretty && false) {
		return "<compound type " + type() + ">";
	}

	return compound_to_string(*this, level, pretty, name());
}

std::string nv_array_base::to_string(unsigned level, bool pretty) const
{
	return compound_to_string(*this, level, pretty, name(), m_is_end);
}

nv_data::nv_data(size_t width)
: m_buf(width, '\0')
{
	if (!width) {
		throw invalid_argument("width must not be 0");
	}
}

string nv_data::to_string(unsigned level, bool pretty) const
{
	return data_to_string(m_buf, level, pretty);
}

csp<nv_val> nv_data::get(const string& name) const
{
	return make_shared<nv_u8>(m_buf[to_index(name, *this)]);
}

void nv_data::set(const string& name, const string& val)
{
	m_buf[to_index(name, *this)] = lexical_cast<uint8_t>(val);
}

istream& nv_data::read(istream& is)
{
	if (is.read(&m_buf[0], m_buf.size())) {
		m_set = true;
	}

	return is;
}

bool nv_mac::parse(const string& str)
{
	auto tok = split(str, ':');
	if (tok.size() == 6) {
		string buf;
		for (auto s : tok) {
			if (s.size() != 2) {
				return false;
			}

			buf += lexical_cast<uint16_t>(s, 16);
		}

		m_buf = buf;
		return true;
	}

	return false;
}

nv_string::nv_string(int flags, size_t width)
: m_flags(flags | ((width && !str_prefix_bytes(flags)) ? flag_fixed_width : 0)), m_width(width)
{}

string nv_string::type() const
{
	string ret;

	if (m_flags & flag_prefix_u8) {
		ret += "p8";
	} else if (m_flags & flag_prefix_u16) {
		ret += "p16";
	} else if (m_flags & flag_fixed_width) {
		ret += "f";
	}

	if (m_flags & flag_size_includes_prefix) {
		ret += "i";
	}

	if (m_flags & flag_require_nul) {
		ret += "z";
	}

	if (m_flags & flag_is_data) {
		ret += "data";
	} else {
		ret += "string";
	}

	if (m_width) {
		ret += "[" + std::to_string(m_width) + "]";
	}

	return ret;
}

string nv_string::to_string(unsigned level, bool pretty) const
{
	if (m_flags & flag_is_data) {
		return data_to_string(m_val, level, pretty);
	} else {
		string val;
		if (m_flags & flag_optional_nul) {
			val = m_val.c_str();
		} else {
			val = m_val;
		}

		return pretty ? '"' + val + '"' : val;
	}
}

bool nv_string::parse(const string& str)
{
	if (str.size() > str_max_length(m_flags, m_width)) {
		return false;
	}

	m_val = str;
	m_set = true;
	return true;
}

istream& nv_string::read(istream& is)
{
	string val;
	size_t size = (m_flags & flag_fixed_width) ? m_width : 0;
	bool zstring = false;

	if (!size) {
		if (m_flags & flag_prefix_u8) {
			size = read_num<uint8_t>(is);
		} else if (m_flags & flag_prefix_u16) {
			size = read_num<uint16_t>(is);
		} else {
			getline(is, val, '\0');
			zstring = true;
		}

		if (size && (m_flags & flag_size_includes_prefix)) {
			size_t min = str_prefix_bytes(m_flags);
			if (size < min) {
				throw runtime_error("size " + std::to_string(size) + " is less than " + std::to_string(min));
			}

			size -= min;
		}
	}

	if (size) {
		val.resize(size);
		is.read(&val[0], val.size());
	}

	if (!is) {
		throw runtime_error("error while reading " + type());
	}

	if (!zstring && (m_flags & flag_require_nul)) {
		if (val.back() != '\0' && !((m_flags & flag_fixed_width) && val.find('\0') != string::npos)) {
			throw runtime_error("expected terminating nul byte in " + data_to_string(val, 0, false) + ", " + std::to_string(val.find('\0')));
		}
		val = val.c_str();
	}

	parse_checked(val);
	return is;
}

ostream& nv_string::write(ostream& os) const
{
	string val = m_val;
	if ((m_flags & flag_fixed_width) && (val.size() < m_width)) {
		val.resize(val.size() + 1);
		val.resize(m_width, '\xff');
	} else if (m_flags & flag_require_nul) {
		val.resize(val.size() + 1);
	}

	size_t size = val.size() + ((m_flags & flag_size_includes_prefix) ? str_prefix_bytes(m_flags) : 0);

	if (m_flags & flag_prefix_u8) {
		write_num<uint8_t>(os, size);
	} else if (m_flags & flag_prefix_u16) {
		write_num<uint16_t>(os, size);
	}

	if (!(os << val)) {
		throw runtime_error("failed to write " + type());
	}

	return os;
}

size_t nv_string::bytes() const
{
	if (m_flags & flag_fixed_width) {
		return m_width;
	}

	return m_val.size() + str_prefix_bytes(m_flags) + str_extra_bytes(m_flags);
}

bool nv_bool::parse(const string& str)
{
	if (str == "1" || str == "true" || str == "yes") {
		m_val = 1;
		m_set = true;
		return true;
	} else if (str == "0" || str == "false" || str == "no") {
		m_val = 0;
		m_set = true;
		return true;
	}

	return false;
}

string nv_magic::to_string(unsigned, bool pretty) const
{
	return magic_to_string(m_buf, pretty, '.');
}

nv_magic::nv_magic(const std::string& magic)
: nv_magic()
{
	parse_checked(magic);
}

nv_magic::nv_magic(uint32_t magic)
: nv_magic()
{
	magic = hton(magic);
	parse_checked(string(reinterpret_cast<const char*>(&magic), 4));
}

bool nv_magic::parse(const string& str)
{
	if (str.size() == 4) {
		m_buf = str;
		return true;
	}

	return false;
}

nv_group::nv_group(const nv_magic& magic, const std::string& name)
: nv_compound(true, name), m_magic(magic)
{}

bool nv_group::init(bool force)
{
	if (nv_compound::init(force)) {
		m_bytes = is_versioned() ? 8 : 6;
		m_width = m_size.num();
		m_set = true;
		return true;
	}

	return false;
}

istream& nv_group::read(istream& is)
{
	if (is_versioned() && !m_version.read(is)) {
		throw runtime_error("failed to read group version");
	}

	logger::t() << "** " << m_magic.to_str() << " " << m_magic.to_pretty() << " " << m_size.num() << " b, version 0x" << to_hex(m_version.num()) << endl;

	auto pos = is.tellg();
	try {
		nv_compound::read(is);
	} catch (const exception& e) {
		if (m_format == fmt_unknown) {
			throw e;
		}

		logger::w() << "failed to parse group " << name() << endl;
		logger::d() << e.what() << endl;
		m_format = fmt_unknown;

		is.clear();
		is.seekg(pos);

		return nv_compound::read(is);
	}

	if (is) {
		//m_bytes += is_versioned() ? 8 : 6;

		if (m_bytes < m_size.num()) {
			sp<nv_val> extra = make_shared<nv_data>(m_size.num() - m_bytes);
			if (!extra->read(is)) {
				throw runtime_error("failed to read remaining " + std::to_string(extra->bytes()) + " bytes");
			}

			logger::t() << "  extra data size is " << extra->bytes() << "b" << endl;
			m_parts.push_back(named("_extra", extra));
			logger::t() << extra->to_pretty() << endl;
			m_bytes += extra->bytes();
		}
	} else {
		if (is.bad()) {
			throw runtime_error(type() + ": read error");
		}

		m_size.num(m_bytes);
		logger::t() << "  truncating group size to " << m_bytes << endl;

		// nv_compound::read() may have set failbit
		is.clear(is.rdstate() & ~ios::failbit);
	}

#if 0
	if (m_bytes != m_size.num()) {
		throw runtime_error("group has trailing data: " + std::to_string(m_bytes) + " / " + m_size.to_string(false));
	}
#endif

	return is;
}

ostream& nv_group::write(ostream& os) const
{
	if (m_bytes > 0xffff) {
		throw runtime_error(type() + ": size " + ::to_string(m_bytes) + " exceeds maximum");
	}

	if (!nv_u16::write(os, m_bytes) || !m_magic.write(os) || (is_versioned() && !m_version.write(os))) {
		throw runtime_error(type() + ": error while writing group header");
		return os;
	}

	if (m_bytes > 8) {
		nv_compound::write(os);
	}

	return os;
}

nv_val::list nv_group::definition() const
{
	if (!m_format) {
		return nv_group::definition(m_format, m_version);
	} else {
		return definition(m_format, m_version);
	}
}

nv_val::list nv_group::definition(int format, const nv_version& ver) const
{
	uint16_t size = m_size.num() - (is_versioned() ? 8 : 6);
	if (size) {
		return {{ "_data", std::make_shared<nv_data>(size) }};
	}

	return {};
}

void nv_group::registry_add(const csp<nv_group>& group)
{
	registry()[group->m_magic] = group;
}

map<nv_magic, csp<nv_group>>& nv_group::registry()
{
	static map<nv_magic, csp<nv_group>> ret;
	return ret;
}

istream& nv_group::read(istream& is, sp<nv_group>& group, int format,
		size_t remaining, const csp<bcm2dump::profile>& p)
{
	nv_u16 size;
	nv_magic magic;

	if (!read_group_header(is, size, magic, remaining)) {
		group = nullptr;
		return is;
	} else if (size.num() > remaining) {
		logger::v() << "group size " << size.to_str() << " exceeds maximum size " << remaining << endl;
		size.num(remaining);
	}

	auto i = registry().find(magic);
	if (i == registry().end()) {
		string name = transform(magic_to_string(magic.raw(), true, 0), ::tolower);
		group = make_shared<nv_group_generic>(magic, "grp_" + name);
	} else {
		group.reset(i->second->clone());
	}

	group->m_size = size;
	group->m_magic = magic;
	group->m_format = format;
	group->m_profile = p;

	return group->read(is);
}

}

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
	return bswapper<T>::ntoh(num);
}

template<typename T> void write_num(ostream& os, T num)
{
	num = bswapper<T>::hton(num);
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

istream& read_group_header(istream& is, nv_u16& size, nv_magic& magic)
{
	if (size.read(is)) {
		if (size.num() < 6) {
			throw runtime_error("group size " + size.to_str() + " too small to be valid");
		} else if (!magic.read(is)) {
			throw runtime_error("failed to read group magic");
		}
	}

	return is;
}

string data_to_string(const string& data, unsigned level, bool pretty)
{
	const unsigned threshold = 24;
	ostringstream ostr;
	bool multiline = pretty && (data.size() > threshold);
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
	if (flags & nv_string_base::flag_prefix_u8) {
		return 0xff;
	} else if (flags & nv_string_base::flag_prefix_u16) {
		return 0xffff;
	}

	return string::npos -1;
}

size_t str_prefix_bytes(int flags)
{
	if (flags & nv_string_base::flag_prefix_u8) {
		return 1;
	} else if (flags & nv_string_base::flag_prefix_u16) {
		return 2;
	}

	return 0;
}

size_t str_extra_bytes(int flags)
{
	return flags & nv_string_base::flag_require_nul ? 1 : 0;
}

size_t str_max_length(int flags, size_t width)
{
	if (width) {
		return width - str_extra_bytes(flags);
	}

	size_t max = str_prefix_max(flags);
	if (flags & nv_string_base::flag_size_includes_prefix) {
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
		throw invalid_argument("requested non-existing member '" + name + "' of type " + type());
	}

	return val;
}

void nv_compound::set(const string& name, const string& val)
{
	// FIXME this function is completely broken
	sp<nv_val> v = const_pointer_cast<nv_val>(get(name));
	if (!v->is_set()) {
		string preceding_unset_name;

		for (auto p : parts()) {
			if (!p.val->is_disabled()) {
				if (p.name == name) {
					break;
				}

				if (p.val->is_set()) {
					preceding_unset_name.clear();
				} else {
					preceding_unset_name = p.name;
				}
			}
		}

		if (!preceding_unset_name.empty()) {
			throw user_error("cannot set '" + name + "' because the previous" +
					+ "element '" + preceding_unset_name + "' is not set");
		}
	}

	ssize_t diff = get(name)->bytes();
	diff -= v->parse_checked(val).bytes();
	m_bytes += diff;
}

csp<nv_val> nv_compound::find(const string& name) const
{
	vector<string> tok = split(name, '.', false, 2);

	for (auto c : parts()) {
		if (c.val->is_disabled()) {
			continue;
		} else if (c.name == tok[0]) {
			if (tok.size() == 2 && c.val->is_compound()) {
				return nv_val_cast<nv_compound>(c.val)->find(tok[1]);
			}
			return c.val;
		}
	}

	return nullptr;
}

bool nv_compound::init(bool force)
{
	if (m_parts.empty() || force) {
		m_parts = definition();
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
			logger::d() << "skipping disabled " << desc(v) << endl;
			continue;
		}

		try {
			if ((m_width && (m_bytes + v.val->bytes() > m_width)) || (!v.val->read(is) && !is.eof())) {
				if (!m_partial) {
					throw runtime_error("pos " + ::to_string(m_bytes) + ": failed to read " + desc(v));
				} else {
					logger::d() << "pos " << m_bytes << ": stopped parsing at " << desc(v) << ", stream=" << !!is
							<< " width=" << m_width << " bytes=" << m_bytes << " val=" << v.val->bytes() << " bytes" << endl;
					m_set = true;
				}
				break;
			} else {

				// check again, because a successful read may have changed the
				// byte count (e.g. an nv_pstring)
				if ((m_width && m_bytes + v.val->bytes() > m_width)) {
					logger::d() << v.val->bytes() << endl;
					throw runtime_error("pos " + ::to_string(m_bytes) + ": variable ends outside of group: " + desc(v));
				}
				logger::d() << "pos " << m_bytes  << ": " + desc(v) << " = " << v.val->to_pretty() << " (" << v.val->bytes() << " b)"<< endl;
				m_bytes += v.val->bytes();
				m_set = true;

				if (is.eof()) {
					break;
				}
			}
		} catch (const exception& e) {
			throw runtime_error("failed at pos " + std::to_string(m_bytes) + " while reading " + desc(v) + ": " + e.what());
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
		logger::d() << "pos " << pos << ": ";
		if (v.val->is_disabled()) {
			logger::d() << v.name << " (disabled)" << endl;
			continue;
		} else if (!v.val->is_set()) {
			if (m_partial) {
				logger::d() << v.name << " (unset)" << endl;
				continue;
			}
			logger::d() << "writing unset " << name() << "." << v.name << endl;
		}

		if (!v.val->write(os)) {
			throw runtime_error("failed to write " + desc(v));
		}

		logger::d() << desc(v) << " = " << v.val->to_pretty() << endl;
		pos += v.val->bytes();
	}

	return os;
}

std::string nv_compound::to_string(unsigned level, bool pretty) const
{
	if (!pretty) {
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

nv_string_base::nv_string_base(int flags, size_t width)
: m_flags(flags | ((width && !str_prefix_bytes(flags)) ? flag_fixed_width : 0)), m_width(width)
{}

string nv_string_base::type() const
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

string nv_string_base::to_string(unsigned level, bool pretty) const
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

bool nv_string_base::parse(const string& str)
{
	if (str.size() > str_max_length(m_flags, m_width)) {
		return false;
	}

	m_val = str;
	m_set = true;
	return true;
}

istream& nv_string_base::read(istream& is)
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

ostream& nv_string_base::write(ostream& os) const
{
	string val = m_val;
	if ((m_flags & flag_fixed_width) && val.size() < m_width) {
		val.resize(m_width);
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

size_t nv_string_base::bytes() const
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
	string str;
	bool ascii = pretty && (isprint(m_buf[0]) || isprint(m_buf[1]));

	for (size_t i = 0; i < 4; ++i) {
		if (!ascii) {
			str += to_hex(m_buf[i]);
		} else if (!isprint(m_buf[i])) {
			str += '.';
		} else {
			str += m_buf[i];
		}
	}

	return str;
}

nv_magic::nv_magic(const std::string& magic)
: nv_magic()
{
	parse_checked(magic);
}

nv_magic::nv_magic(uint32_t magic)
: nv_magic()
{
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
		return true;
	}

	return false;
}

istream& nv_group::read(istream& is)
{
	if (is_versioned() && !m_version.read(is)) {
		throw runtime_error("failed to read group version");
	}

	logger::d() << "** " << m_magic.to_str() << " " << m_magic.to_pretty() << " " << m_size.num() << " b, version 0x" << to_hex(m_version.num()) << endl;

	if (nv_compound::read(is)) {
		//m_bytes += is_versioned() ? 8 : 6;

		if (m_bytes < m_size.num()) {
			sp<nv_val> extra = make_shared<nv_data>(m_size.num() - m_bytes);
			if (!extra->read(is)) {
				throw runtime_error("failed to read remaining " + std::to_string(extra->bytes()) + " bytes");
			}

			logger::d() << "  read " << m_bytes << " b , group size is " << m_size.num() << "; extra data size is " << extra->bytes() << "b" << endl;
			m_parts.push_back(named("extra", extra));
			logger::d() << extra->to_pretty() << endl;
			m_bytes += extra->bytes();
		}
	} else {
		throw runtime_error(type() + ": unspecified error while reading data");
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
	if (!m_size.write(os) || !m_magic.write(os) || (is_versioned() && !m_version.write(os))) {
		throw runtime_error(type() + ": error while writing group header");
		return os;
	}

	return nv_compound::write(os);
}

nv_val::list nv_group::definition() const
{
	return definition(m_type, m_version);
}

nv_val::list nv_group::definition(int type, const nv_version& ver) const
{
	uint16_t size = m_size.num() - (is_versioned() ? 8 : 6);
	if (size) {
		return {{ "data", std::make_shared<nv_data>(size) }};
	}

	return {};
}

map<nv_magic, csp<nv_group>> nv_group::s_registry;

void nv_group::registry_add(const csp<nv_group>& group)
{
	s_registry[group->m_magic] = group;
}

istream& nv_group::read(istream& is, sp<nv_group>& group, int type, size_t maxsize)
{
	nv_u16 size;
	nv_magic magic;

	if (!read_group_header(is, size, magic)) {
		return is;
	} else if (size.num() > maxsize) {
		throw runtime_error("size of group " + magic.to_str() + " " + size.to_str()
				+ " exceeds " + std::to_string(maxsize));
		//is.setstate(ios::failbit);
		//return is;
	}

	auto i = s_registry.find(magic);
	if (i == s_registry.end()) {
		string name = magic.to_pretty();
		name = transform(name.substr(0, name.find('.')), ::tolower);
		group = make_shared<nv_group_generic>(magic, "grp_" + name);
	} else {
		group.reset(i->second->clone());
	}

	group->m_size = size;
	group->m_magic = magic;
	group->m_type = type;

	return group->read(is);
}

}

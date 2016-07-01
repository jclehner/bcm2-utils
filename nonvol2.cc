#include <iostream>
#include <string>
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

void read_group_header(istream& is, nv_u16& size, nv_magic& magic)
{
	if (!size.read(is)) {
		throw runtime_error("failed to read group size");
	} else if (size.num() < 6) {
		throw runtime_error("group size too small to be valid");
	} else if (!magic.read(is)) {
		throw runtime_error("failed to read group magic");
	}
}
}

nv_val::csp nv_val::get(const string& name) const
{
	throw runtime_error("requested member '" + name + "' of non-compound type " + type());
}

void nv_val::set(const string& name, const string& val)
{
	throw runtime_error("requested member '" + name + "' of non-compound type " + type());
}

void nv_val::parse_checked(const std::string& str)
{
	if (!parse(str)) {
		throw runtime_error("conversion to " + type() + " failed: '" + str + "'");
	}
}

nv_compound::nv_compound(bool partial, size_t width, bool internal) : m_partial(partial), m_width(width)
{
	if (!width && !internal) {
		throw invalid_argument("width must not be 0");
	}
}

bool nv_compound::parse(const string& str)
{
	throw invalid_argument("cannot directly set value of compound type " + type());
}

nv_val::csp nv_compound::get(const string& name) const
{
	auto val = find(name);
	if (!val) {
		throw invalid_argument("requested non-existing member '" + name + "' of type " + type());
	}

	return val;
}

void nv_compound::set(const string& name, const string& val)
{
	if (!const_pointer_cast<nv_val>(get(name))->parse(val)) {
		throw invalid_argument("invalid " + type() + ": '" + val + "'");
	}
}

nv_val::sp nv_compound::find(const string& name) const
{
	for (auto c : m_parts) {
		if (c.name == name) {
			return c.val;
		}
	}

	return nullptr;
}

void nv_compound::init(bool force)
{
	if (m_parts.empty() || force) {
		m_parts = definition();
		m_set = false;
	}
}

istream& nv_compound::read(istream& is)
{
	clear();

	for (auto v : m_parts) {
		if ((m_width && m_bytes + v.val->bytes() >= m_width) || !v.val->read(is)) {
			if (!m_partial) {
				throw runtime_error("pos " + to_string(m_bytes) + ": failed to read " + desc(v));
			}
			break;
		} else {
			// check again, because a successful read may have changed the
			// byte count (e.g. an nv_pstring)
			if ((m_width && m_bytes + v.val->bytes() >= m_width)) {
				throw runtime_error("pos " + to_string(m_bytes) + ": variable ends outside of group: " + desc(v));
			}
			m_bytes += v.val->bytes();
		}
	}

	return is;
}

ostream& nv_compound::write(ostream& os) const
{
	if (m_parts.empty()) {
		throw runtime_error("attempted to serialize uninitialized compound");
	}

	for (auto v : m_parts) {
		if (m_partial && !v.val->is_set()) {
			break;
		} else if (!v.val->write(os)) {
			throw runtime_error("failed to write " + desc(v));
		}
	}

	return os;
}

nv_data::nv_data(size_t width)
: m_buf(width, '\0')
{
	if (!width) {
		throw invalid_argument("width must not be 0");
	}
}

string nv_data::to_string(bool quote) const
{
	string str;

	for (char c : m_buf) {
		if (!str.empty()) {
			//str += '';
		}
		str += "\\x" + to_hex(c & 0xff, 2);
	}

	return quote ? '"' + str + '"' : str;
}

nv_val::csp nv_data::get(const string& name) const
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

bool nv_string::parse(const string& str)
{
	if (!m_width || str.size() < m_width) {
		m_val = str;
		m_set = true;
		return true;
	}

	return false;
}

istream& nv_zstring::read(istream& is)
{
	string val;

	if (m_width) {
		val.resize(m_width);
		if (!is.read(&val[0], val.size()) || val.back() != '\0') {
			return is;
		}
		// reduce the string to its actual size
		val = string(val.c_str());
	} else {
		if (!getline(is, val, '\0')) {
			return is;
		}
	}

	if (!parse(val)) {
		is.setstate(ios::failbit);
	}

	return is;
}

ostream& nv_zstring::write(ostream& os) const
{
	string val = m_val;
	val.resize(m_width ? m_width : val.size() + 1);
	return os.write(val.data(), val.size());
}

istream& nv_pstring::read(istream& is)
{
	uint16_t len;
	if (!is.read(reinterpret_cast<char*>(&len), 2)) {
		return is;
	}

	string val(ntohs(len), '\0');
	if (!is.read(&val[0], val.size()) || !parse(val)) {
		is.setstate(ios::failbit);
	}

	return is;
}

ostream& nv_pstring::write(ostream& os) const
{
	uint16_t len = htons(m_val.size() & 0xffff);
	if (!os.write(reinterpret_cast<const char*>(&len), 2)) {
		return os;
	}

	return os.write(m_val.data(), m_val.size());
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

string nv_magic::to_string(bool) const
{
	string str;
	bool ascii = true;

	for (size_t i = 0; i < 4; ++i) {
		if (!ascii) {
			str += to_hex(m_buf[i]);
		} else if (!isprint(m_buf[i])) {
			i = 0;
			ascii = false;
			str.clear();
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

nv_group::nv_group(const nv_magic& magic, int type)
: nv_compound(true), m_magic(magic), m_type(type)
{}

istream& nv_group::read(istream& is)
{
#if 0
	read_group_header(is, m_size, m_magic);
#endif
	if (is_versioned() && !m_version.read(is)) {
		throw runtime_error("failed to read group version");
	}

	m_bytes = is_versioned() ? 8 : 6;

	if (nv_compound::read(is)) {
		if (m_bytes < m_size.num()) {
			nv_val::sp extra = make_shared<nv_data>(m_size.num() - m_bytes);
			if (!extra->read(is)) {
				throw runtime_error("failed to read remaining " + std::to_string(extra->bytes()) + " bytes");
			}

			m_parts.push_back(named("extra", extra));
			m_bytes = m_size.num();
		}
	}

	return is;
}

ostream& nv_group::write(ostream& os) const
{
	if (!m_size.write(os) || !m_magic.write(os)) {
		return os;
	} else if (is_versioned() && !m_version.write(os)) {
		return os;
	}

	return nv_compound::write(os);
}

nv_val::list nv_group::definition(int type, int maj, int min) const
{
	uint16_t size = m_size.num() - (is_versioned() ? 8 : 6);
	{ return {{ "data", std::make_shared<nv_data>(size) }}; }
}

map<nv_magic, nv_group::sp> nv_group::s_registry;

void nv_group::registry_add(const sp& group)
{
	s_registry[group->m_magic] = group;
}

istream& nv_group::read(istream& is, sp& group)
{
	nv_u16 size;
	nv_magic magic;

	read_group_header(is, size, magic);

	auto i = s_registry.find(magic);
	if (i == s_registry.end()) {
		logger::d() << "no group definition for " << magic.to_string(false) << endl;
		group = make_shared<nv_group>();
	} else {
		logger::d() << "found group definition for " << magic.to_string(false) << endl;
		group.reset(i->second->clone());
	}

	group->m_size = size;
	group->m_magic = magic;

	return group->read(is);
}

}

#ifndef BCM2CFG_NONVOL_H
#define BCM2CFG_NONVOL_H
#include <arpa/inet.h>
#include <iostream>
#include <limits>
#include <vector>
#include <memory>
#include <string>
#include <map>
#include "util.h"

using bcm2dump::sp;
using bcm2dump::csp;

namespace bcm2cfg {

struct serializable
{
	virtual ~serializable() {}
	virtual std::istream& read(std::istream& is) = 0;
	virtual std::ostream& write(std::ostream& os) const = 0;
};

struct cloneable
{
	virtual ~cloneable() {}
	virtual cloneable* clone() const = 0;
};

template<class T> struct nv_type
{
	static std::string name()
	{
		return T().type();
	}

	static size_t bytes()
	{
		return T().bytes();
	}
};

template<class To, class From> sp<To> nv_val_cast(const From& from);

class nv_val : public serializable
{
	public:
	struct named
	{
		named(const std::string& name, const sp<nv_val>& val)
		: name(name), val(val) {}

		std::string name;
		sp<nv_val> val;
	};
	typedef std::vector<named> list;

	virtual ~nv_val() {}

	virtual std::string type() const = 0;
	virtual std::string to_string(unsigned level, bool pretty) const = 0;

	virtual std::string to_str() const final
	{ return to_string(0, false); }

	virtual std::string to_pretty(unsigned level = 0) const final
	{ return to_string(level, true); }

	virtual bool parse(const std::string& str) = 0;
	virtual nv_val& parse_checked(const std::string& str) final;

	bool is_set() const
	{ return m_set; }

	// when default-constructed, return the minimum byte count
	// required for this type. after a value has been set, return
	// the number of bytes required to store this value
	virtual size_t bytes() const = 0;

	// for compound types, provide a facility to get/set only a part
	virtual csp<nv_val> get(const std::string& name) const;

	template<class T> csp<T> get_as(const std::string& name) const
	{ return nv_val_cast<const T>(get(name)); }

	virtual void set(const std::string& name, const std::string& val);

	virtual const list& parts() const
	{ return m_parts; }

	virtual void disable(bool disable)
	{ m_disabled = disable; }
	virtual bool is_disabled() const
	{ return m_disabled; }

	virtual bool is_compound() const
	{ return false; }

	friend std::ostream& operator<<(std::ostream& os, const nv_val& val)
	{ return (os << val.to_pretty()); }

	protected:
	bool m_disabled = false;
	bool m_set = false;
	list m_parts;
};

template<class To, class From> sp<To> nv_val_cast(const From& from)
{
	sp<To> p = std::dynamic_pointer_cast<To>(from);
	if (!p) {
		throw std::invalid_argument("failed cast: " + from->type() + " (" + from->to_str() + ") -> " + nv_type<To>::name());
	}

	return p;
}

class nv_compound : public nv_val
{
	public:
	virtual std::string to_string(unsigned level, bool pretty) const override;

	virtual const std::string& name() const
	{ return m_name; }

	virtual void rename(const std::string& name)
	{ m_name = name; }

	virtual bool parse(const std::string& str) override;

	virtual csp<nv_val> get(const std::string& name) const override;
	virtual void set(const std::string& name, const std::string& val) override;
	// like get, but shouldn't throw
	virtual csp<nv_val> find(const std::string& name) const;

	virtual bool init(bool force = false);
	virtual void clear()
	{ init(true); }

	virtual size_t bytes() const override
	{ return m_bytes ? m_bytes : m_width; }

	virtual std::istream& read(std::istream& is) override;
	virtual std::ostream& write(std::ostream& os) const override;

	virtual bool is_compound() const final
	{ return true; }

	protected:
	nv_compound(bool partial, const std::string& name = "")
	: nv_compound(partial, 0, name) {}
	nv_compound(bool partial, size_t width, const std::string& name = "")
	: m_partial(partial), m_width(width), m_name(name) {}
	virtual list definition() const = 0;

	bool m_partial = false;
	// expected final size
	size_t m_width = 0;
	// actual size
	size_t m_bytes = 0;

	private:
	std::string m_name;
};

template<> struct nv_type<nv_compound>
{
	static std::string name()
	{ return "nv_compound"; }

	static size_t bytes()
	{ return 0; }
};

class nv_compound_def final : public nv_compound
{
	public:
	nv_compound_def(const std::string& name, const nv_compound::list& def, bool partial = false)
	: nv_compound(partial), m_def(def) { nv_compound::rename(name); }

	virtual std::string type() const override
	{ return name(); }

	protected:
	virtual list definition() const override
	{ return m_def; }

	private:
	nv_compound::list m_def;
};

class nv_array_base : public nv_compound
{
	public:
	// used by to_string to prematurely stop printing elements
	// in a fixed-size list
	typedef std::function<bool(const csp<nv_val>&)> is_end_func;

	virtual std::string to_string(unsigned level, bool pretty) const override;

	protected:
	nv_array_base(size_t width) : nv_compound(false, width), m_is_end(nullptr) {}
	is_end_func m_is_end;
};

template<class T, class I, bool L> class nv_array_generic : public nv_array_base
{
	public:
	nv_array_generic(I n = 0)
	: nv_array_base(n * nv_type<T>::bytes()), m_memb(), m_count(n)
	{
		if (!L && !n) {
			throw std::invalid_argument("size must not be 0");
		}
	}
	virtual ~nv_array_generic() {}

	virtual std::string type() const override
	{
		return std::string(L ? "list" : "array") + "<" + m_memb.type() + ">"
			+ (m_count ? "[" + std::to_string(m_count) + "]" : "");
	}

	virtual std::istream& read(std::istream& is) override
	{
		if (L) {
			if (!m_count && !is.read(reinterpret_cast<char*>(&m_count), sizeof(I))) {
				return is;
			}
			m_count = bcm2dump::bswapper<I>::ntoh(m_count);
		}

		return nv_compound::read(is);
	}

	virtual std::ostream& write(std::ostream& os) const override
	{
		if (L) {
			I count = bcm2dump::bswapper<I>::hton(m_count);
			if (!os.write(reinterpret_cast<const char*>(&count), sizeof(I))) {
				return os;
			}
		}

		return nv_compound::write(os);
	}

	virtual size_t bytes() const override
	{ return nv_compound::bytes() + (L ? sizeof(I) : 0); }

	protected:

	virtual list definition() const override
	{
		list ret;

		for (I i = 0; i < m_count; ++i) {
			ret.push_back({ std::to_string(i), std::make_shared<T>()});
		}

		return ret;
	}

	private:
	T m_memb;
	I m_count = 0;
};

template<typename T, size_t N = 0> class nv_array : public nv_array_generic<T, size_t, false>
{
	public:
	typedef std::function<bool(const csp<T>&)> is_end_func;

	// arguments to is_end shall only be of type T, so an unchecked
	// dynamic_cast can be safely used
	nv_array(size_t n = N, const is_end_func& is_end = nullptr)
	: nv_array_generic<T, size_t, false>(n), m_is_end(is_end), m_n(n)
	{
		if (is_end) {
			nv_array_base::m_is_end = [this] (const csp<nv_val>& val) {
				return m_is_end(nv_val_cast<const T>(val));
			};
		}
	}
	virtual ~nv_array() {}

	private:
	is_end_func m_is_end;
	size_t m_n;
};

template<typename T, typename I> using nv_plist = nv_array_generic<T, I, true>;
template<typename T> using nv_p8list = nv_plist<T, uint8_t>;
template<typename T> using nv_p16list = nv_plist<T, uint8_t>;

class nv_data : public nv_val
{
	public:
	explicit nv_data(size_t width);

	virtual std::string type() const override
	{ return "data[" + std::to_string(m_buf.size()) + "]"; }

	virtual std::string to_string(unsigned level, bool pretty) const override;
	virtual bool parse(const std::string& str) override
	{ return false; }

	virtual std::istream& read(std::istream& is) override;
	virtual std::ostream& write(std::ostream& os) const override
	{  return os.write(m_buf.data(), m_buf.size()); }

	virtual size_t bytes() const override
	{ return m_buf.size(); }

	virtual csp<nv_val> get(const std::string& name) const override;
	virtual void set(const std::string& name, const std::string& val) override;

	protected:
	std::string m_buf;

};

class nv_unknown : public nv_data
{
	public:
	nv_unknown(size_t width) : nv_data(width) {}

	std::string to_string(unsigned, bool) const override
	{ return "<" + std::to_string(bytes()) + " bytes>"; }
};

template<int N> class nv_ip : public nv_data
{
	static_assert(N == 4 || N == 6, "N must be either 4 or 6");
	static constexpr int AF = (N == 4 ? AF_INET : AF_INET6);

	public:
	nv_ip() : nv_data(N == 4 ? 4 : 16) {}

	std::string type() const override
	{ return "ip" + std::to_string(N); }

	std::string to_string(unsigned level, bool pretty) const override
	{
		char buf[32];
		if (!inet_ntop(AF, m_buf.data(), buf, sizeof(buf)-1)) {
			return nv_data::to_string(level, pretty);
		}
		return buf;
	}

	bool parse(const std::string& str) override
	{
		return inet_pton(AF, str.c_str(), &m_buf[0]) == 1;
	}
};

class nv_ip4 : public nv_ip<4> {};
class nv_ip6 : public nv_ip<6> {};

class nv_mac : public nv_data
{
	public:
	nv_mac() : nv_data(6) {}
};

class nv_string : public nv_val
{
	public:
	explicit nv_string(size_t width = 0) : m_width(width) {}

	// it doesn't matter to the user whether it's a nv_pstring or a nv_zstring
	virtual std::string type() const override
	{ return "string" + (m_width ? "[" + std::to_string(m_width) + "]" : ""); }

	virtual std::string to_string(unsigned, bool pretty) const override
	{ return pretty ? '"' + m_val + '"' : m_val; }

	virtual bool parse(const std::string& str) override;

	bool operator!=(const nv_string& other)
	{ return m_val == other.m_val; }

	protected:
	size_t m_width;
	std::string m_val;
};

// <string><nul>
class nv_zstring : public nv_string
{
	public:
	explicit nv_zstring(size_t width = 0) : nv_string(width) {}

	virtual std::istream& read(std::istream& is) override;
	virtual std::ostream& write(std::ostream& os) const override;

	virtual size_t bytes() const override
	{ return m_width ? m_width : m_val.size() + 1; }
};

// <len16><string>
class nv_p16string : public nv_string
{
	public:
	explicit nv_p16string(size_t width = 0) : nv_string(width) {}

	virtual bool parse(const std::string& str) override
	{ return str.size() <= 0xffff ? nv_string::parse(str) : false; }

	virtual std::istream& read(std::istream& is) override;
	virtual std::ostream& write(std::ostream& os) const override;

	virtual size_t bytes() const override
	{ return 2 + m_val.size(); }
};

class nv_p8string_base : public nv_string
{
	public:
	virtual std::istream& read(std::istream& is) override;
	virtual std::ostream& write(std::ostream& os) const override;
	virtual bool parse(const std::string& str) override
	{ return str.size() <= 0xfe ? nv_string::parse(str) : false; }

	virtual std::string to_string(unsigned level, bool pretty) const override;

	virtual size_t bytes() const override
	{ return 1 + m_val.size() + (m_nul ? 1 : 0); }

	protected:
	nv_p8string_base(bool nul, bool data, size_t width = 0) : nv_string(width), m_nul(nul), m_data(data) {}
	bool m_nul;
	bool m_data;

	//{ return m_val.empty() ? 1 : 2 + m_val.size(); }
};


// <len8><string>[<nul>]
class nv_p8string : public nv_p8string_base
{
	public:
	nv_p8string(size_t width = 0) : nv_p8string_base(false, false, width) {}
};

// <len8><string>
class nv_p8data : public nv_p8string_base
{
	public:
	nv_p8data(size_t width = 0) : nv_p8string_base(false, true, width) {}
};

// <len8><string><nul>
class nv_p8zstring : public nv_p8string_base
{
	public:
	explicit nv_p8zstring(size_t width = 0) : nv_p8string_base(true, false, width) {}
};

template<class T, class H,
		T MIN = std::numeric_limits<T>::min(),
		T MAX = std::numeric_limits<T>::max()>
class nv_num : public nv_val
{
	public:
	typedef T num_type;

	explicit nv_num(bool hex = false) : m_val(0), m_hex(hex) {}
	nv_num(T val, bool hex) : m_val(val), m_hex(hex) { m_set = true; }

	virtual void hex(bool hex = true)
	{ m_hex = hex; }

	virtual std::string type() const override
	{
		std::string name = H::name();
		if (MIN != std::numeric_limits<T>::min() || MAX != std::numeric_limits<T>::max()) {
			name += "<" + std::to_string(MIN) + "," + std::to_string(MAX) + ">";
		}

		return name;
	}

	virtual std::string to_string(unsigned, bool pretty) const override
	{
		std::string str;

		if (!m_hex) {
			str = std::to_string(m_val);
		} else {
			str = "0x" + bcm2dump::to_hex(m_val);
		}

		if (pretty && (m_val < MIN || m_val > MAX)) {
			str += " (out of range)";
		}

		return str;
	}

	virtual bool parse(const std::string& str) override
	{
		try {
			T val = bcm2dump::lexical_cast<T>(str, 0);
			if (val < MIN || val > MAX) {
				return false;
			}

			m_val = true;
			m_set = true;
			return true;
		} catch (const bcm2dump::bad_lexical_cast& e) {
			return false;
		}
	}

	virtual std::istream& read(std::istream& is) override
	{
		if (is.read(reinterpret_cast<char*>(&m_val), sizeof(T))) {
			m_val = bcm2dump::bswapper<T>::ntoh(m_val);
			m_set = true;
		}

		return is;
	}

	virtual std::ostream& write(std::ostream& os) const override
	{
		T swapped = bcm2dump::bswapper<T>::hton(m_val);
		return os.write(reinterpret_cast<const char*>(&swapped), sizeof(T));
	}

	virtual size_t bytes() const override
	{ return sizeof(T); }

	virtual T num() const
	{ return m_val; }

	bool operator!=(const nv_num<T, H>& other)
	{ return m_val == other.m_val; }

	protected:
	T m_val;
	bool m_hex = false;
};

namespace detail {

template<typename T> struct num_name
{
	static std::string name();
};

template<> struct num_name<uint8_t>
{
	static std::string name()
	{ return "u8"; }
};

template<> struct num_name<uint16_t>
{
	static std::string name()
	{ return "u16"; }
};

template<> struct num_name<uint32_t>
{
	static std::string name()
	{ return "u32"; }
};

template<> struct num_name<uint64_t>
{
	static std::string name()
	{ return "u64"; }
};
}

// defines name (unlimited range), name_r (custom range) and name_m (custom maximum)
#define NV_NUM_DEF(name, num_type) \
	template<num_type MIN, num_type MAX> \
	using name ## _r = nv_num<num_type, detail::num_name<num_type>, MIN, MAX>; \
	template<num_type MAX> using name ## _m = name ## _r<0, MAX>; \
	typedef name ## _r<std::numeric_limits<num_type>::min(), std::numeric_limits<num_type>::max()> name \

NV_NUM_DEF(nv_u8, uint8_t);
NV_NUM_DEF(nv_u16, uint16_t);
NV_NUM_DEF(nv_u32, uint32_t);
NV_NUM_DEF(nv_u64, uint64_t);

class nv_bool : public nv_u8_r<0, 1>
{
	public:
	virtual std::string type() const override
	{ return "bool"; }

	virtual bool parse(const std::string& str) override;
};

template<typename T> class nv_enum_bitmask : public T
{
	public:
	typedef typename T::num_type num_type;
	typedef typename std::map<num_type, std::string> valmap;
	typedef std::vector<std::string> valvec;

	virtual ~nv_enum_bitmask() {}

	protected:
	nv_enum_bitmask(const std::string& name, const valvec& vals) : nv_enum_bitmask(name, vals.size()) { m_vec = vals; }
	nv_enum_bitmask(const std::string& name, const valmap& vals) : nv_enum_bitmask(name, vals.size()) { m_map = vals; }

	bool str_to_num(const std::string& str, num_type& num) const
	{
		for (num_type i = 0; i < str.size(); ++i) {
			if (m_vec[i] == str) {
				num = i;
				return true;
			}
		}

		for (auto v : m_map) {
			if (v.second == str) {
				num = v.first;
				return true;
			}
		}

		try {
			num = bcm2dump::lexical_cast<num_type>(str, 0);
			return true;
		} catch (const bcm2dump::bad_lexical_cast& e) {}

		return false;
	}

	std::string num_to_str(const num_type& num, bool bitmask, bool pretty) const
	{
		std::string str;

		if (!m_map.empty()) {
			auto i = m_map.find(bitmask ? (1 << num) : num);
			if (i != m_map.end()) {
				str = i->second;
			}
		} else if (!m_vec.empty() && num < m_vec.size()) {
			str = m_vec[num];
		}

		return str;
	}

	std::string m_name;

	private:
	nv_enum_bitmask(const std::string& name, size_t n)
	: m_name(name)
	{
		if (n > std::numeric_limits<num_type>::max()) {
			throw std::invalid_argument("number of enum elements exceeds maximum for " + nv_type<T>::name());
		}
	}

	valmap m_map;
	valvec m_vec;
};

template<class T> class nv_enum : public nv_enum_bitmask<T>
{
	typedef nv_enum_bitmask<T> super;

	public:
	nv_enum(const std::string& name, const typename super::valmap& vals)
	: super(name, vals) {}
	nv_enum(const std::string& name, const typename super::valvec& vals)
	: super(name, vals) {}

	virtual ~nv_enum() {}

	virtual std::string type() const override
	{
		std::string name = super::m_name;
		return name.empty() ? "enum" : name;
	}

	virtual std::string to_string(unsigned, bool pretty) const override
	{
		std::string str = super::num_to_str(T::m_val, false, pretty);
		return str.empty() ? type() + "(" + T::to_string(0, pretty) + ")" : str;
	}

	virtual bool parse(const std::string& str) override
	{
		return super::str_to_num(str, T::m_val);
	}
};

template<class T> class nv_bitmask : public nv_enum_bitmask<T>
{
	typedef typename nv_enum_bitmask<T>::num_type num_type;
	typedef nv_enum_bitmask<T> super;

	public:
	nv_bitmask()
	: nv_bitmask("", typename super::valvec {}) {}
	nv_bitmask(const typename super::valmap& vals)
	: super("", vals) {}
	nv_bitmask(const typename super::valvec& vals)
	: super("", vals) {}
	nv_bitmask(const std::string& name, const typename super::valmap& vals)
	: super(name, vals) {}
	nv_bitmask(const std::string& name, const typename super::valvec& vals)
	: super(name, vals) {}

	virtual ~nv_bitmask() {}

	virtual std::string type() const override
	{
		std::string name = super::m_name;
		return name.empty() ? "bitmask" : name;
	}

	virtual std::string to_string(unsigned, bool pretty) const override
	{
		if (T::m_val == 0) {
			return "0x" + bcm2dump::to_hex(T::m_val);
		}

		std::string ret;

		for (size_t i = 0; i != sizeof(num_type) * 8; ++i) {
			num_type flag = 1 << i;
			if (T::m_val & flag) {
				if (!ret.empty()) {
					ret += pretty ? " | " : "|";
				}
				std::string str = super::num_to_str(i, true, pretty);
				ret += str.empty() ? "0x" + bcm2dump::to_hex(flag) : str;
			}
		}

		return ret;
	}

	virtual bool parse(const std::string& str) override
	{
		if (!str.empty()) {
			num_type n;
			if (str[0] == '+' || str[0] == '-') {
				if (!super::str_to_num(str.substr(1), n)) {
					return false;
				}

				if (str[0] == '+') {
					T::m_val |= n;
				} else {
					T::m_val &= ~n;
				}
				return true;
			} else if(super::str_to_num(str, n)) {
				T::m_val = n;
				return true;
			}
		}

		return false;
	}
};


class nv_magic : public nv_data
{
	public:
	nv_magic() : nv_data(4) {}
	nv_magic(const std::string& magic);
	nv_magic(uint32_t magic);

	virtual std::string type() const override
	{ return "magic"; }

	virtual bool parse(const std::string& str) override;

	virtual std::string to_string(unsigned, bool) const override;

	uint32_t as_num() const
	{ return ntohl(*reinterpret_cast<const uint32_t*>(m_buf.data())); }

	bool operator<(const nv_magic& other) const
	{ return m_buf < other.m_buf; }

	bool operator==(const nv_magic& other) const
	{ return m_buf == other.m_buf; }

	bool operator!=(const nv_magic& other) const
	{ return !(*this == other); }

};

class nv_version : public nv_u16
{
	public:
	nv_version() {}
	nv_version(uint8_t maj, uint8_t min)
	{ m_val = maj << 8 | min; }

	virtual std::string type() const override
	{ return "version"; }

	virtual std::string to_string(unsigned, bool) const override
	{ return std::to_string(m_val >> 8) + "." + std::to_string(m_val & 0xff); }

	bool operator==(const nv_version& other)
	{ return m_val == other.m_val; }

	bool operator<(const nv_version& other)
	{ return m_val < other.m_val; }

	uint8_t major() const
	{ return m_val >> 8; }

	uint8_t minor() const
	{ return m_val & 0xff; }
};

class nv_group : public nv_compound, public cloneable
{
	public:
	static constexpr int type_unknown = 0;
	static constexpr int type_perm = 1;
	static constexpr int type_dyn = 2;
	static constexpr int type_cfg = 3;

	nv_group(uint32_t magic, const std::string& name = "")
	: nv_group(nv_magic(magic), name) {}

	nv_group(const std::string& magic, const std::string& name = "")
	: nv_group(nv_magic(magic), name) {}

	nv_group(const nv_magic& magic, const std::string& name);

	virtual bool is_versioned() const
	{ return true; }

	virtual std::string type() const override
	{ return "group[" + m_magic.to_str() + "]"; }

	virtual std::ostream& write(std::ostream& os) const override;

	static std::istream& read(std::istream& is, sp<nv_group>& group, int type);
	static void registry_add(const csp<nv_group>& group);
	static const auto& registry()
	{ return s_registry; }

	virtual nv_group* clone() const override = 0;

	virtual const nv_magic& magic() const
	{ return m_magic; }

	virtual const nv_version& version() const
	{ return m_version; }

	bool init(bool force) override;

	protected:
	virtual list definition() const override final;
	virtual list definition(int type, const nv_version& ver) const;
	virtual std::istream& read(std::istream& is) override;

	nv_u16 m_size;
	nv_magic m_magic;
	nv_version m_version;
	int m_type = type_unknown;

	private:
	static std::map<nv_magic, csp<nv_group>> s_registry;

};

class nv_group_generic : public nv_group
{
	public:
	using nv_group::nv_group;

	virtual nv_group_generic* clone() const override
	{ return new nv_group_generic(*this); }
};

/*
class nv_group : public serializable
{
	public:
	typedef std::shared_ptr<nv_group> sp;
	struct var
	{
		var(const std::string& name, const nv_val::sp& val)
		: name(name), val(val) {}

		const std::string name;
		nv_val::sp val;
	};

	virtual std::string description() const;

	virtual const nv_magic& magic() const
	{ return m_magic; }

	virtual const nv_version& version() const
	{ return m_version; }

	virtual uint16_t bytes() const override
	{ return m_size.get(); }


	virtual const std::vector<var>& vars() const
	{ return m_vars; }

	protected:
	virtual std::vector<var> perm(uint8_t maj, uint8_t min) const

	virtual std::vector<var> dyn(uint8_t maj, uint8_t min) const
	{ return {{ "data", std::make_shared<nv_data>(bytes()) }}; }

	nv_magic m_magic;
	nv_version m_version;
	nv_u16 m_size;
	std::vector<var> m_vars;
	bool m_dynamic = true;
};
*/

// nv_vals: bool, nv_u8, nv_u16, nv_u32, nv_u64, ip4, ip6, mac, 
}

template struct bcm2dump_def_comparison_operators<bcm2cfg::nv_version>;

#endif

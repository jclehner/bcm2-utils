#ifndef BCM2CFG_NONVOL_H
#define BCM2CFG_NONVOL_H
#include <arpa/inet.h>
#include <iostream>
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

template<class T> struct nv_type_name
{
	static std::string get()
	{
		return T().type();
	}
};

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
	{
		csp<nv_val> val = get(name);
		csp<T> ret = std::dynamic_pointer_cast<const T>(val);
		if (!ret) {
			throw std::invalid_argument("failed cast " + val->type() + " -> " + nv_type_name<T>::get());
		}

		return ret;
	}

	virtual void set(const std::string& name, const std::string& val);

	virtual const list& parts() const
	{ return m_parts; }

	protected:

	bool m_set = false;
	list m_parts;
};

class nv_compound : public nv_val
{
	public:
	nv_compound(bool partial, size_t width)
	: nv_compound(partial, width, false) {}

	virtual std::string to_string(unsigned level, bool pretty) const override;

	virtual bool parse(const std::string& str) override;

	virtual csp<nv_val> get(const std::string& name) const override;
	virtual void set(const std::string& name, const std::string& val) override;

	virtual bool init(bool force = false);
	virtual void clear()
	{ init(true); }

	virtual size_t bytes() const override
	{ return m_bytes ? m_bytes : m_width; }

	virtual std::istream& read(std::istream& is) override;
	virtual std::ostream& write(std::ostream& os) const override;

	protected:
	nv_compound(bool partial)
	: nv_compound(partial, 0, true) {}
	// like get, but shouldn't throw
	virtual sp<nv_val> find(const std::string& name) const;
	virtual list definition() const = 0;

	bool m_partial = false;
	// expected final size
	size_t m_width = 0;
	// actual size
	size_t m_bytes = 0;

	private:
	nv_compound(bool partial, size_t width, bool internal);
};

class nv_array_base : public nv_compound
{
	public:
	// used by to_string to prematurely stop printing elements
	// in a fixed-size list
	typedef std::function<bool(const csp<nv_val>&)> is_end_func;

	virtual std::string to_string(unsigned level, bool pretty) const override;

	protected:
	nv_array_base() : nv_compound(false), m_is_end(nullptr) {}
	is_end_func m_is_end;
};

template<class T, class I, bool L> class nv_array_generic : public nv_array_base
{
	public:
	nv_array_generic(I n = 0)
	: m_memb(), m_count(n)
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

template<typename T> class nv_array : public nv_array_generic<T, size_t, false>
{
	public:
	typedef std::function<bool(const csp<T>&)> is_end_func;

	// arguments to is_end shall only be of type T, so an unchecked
	// dynamic_cast can be safely used
	nv_array(size_t n, const is_end_func& is_end = nullptr)
	: nv_array_generic<T, size_t, false>(n), m_is_end(is_end)
	{
		if (!n) {
			throw std::invalid_argument("array size must not be 0");
		}

		if (m_is_end) {
			nv_array_base::m_is_end = [this] (const csp<nv_val>& val) {
				return m_is_end(std::dynamic_pointer_cast<const T>(val));
			};
		}
	}
	virtual ~nv_array() {}

	private:
	is_end_func m_is_end;
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

template<class T, class H> class nv_num : public nv_val
{
	public:
	explicit nv_num(bool hex = false) : m_val(0), m_hex(hex) {}
	nv_num(T val, bool hex) : m_val(val), m_hex(hex) { m_set = true; }

	virtual void hex(bool hex = true)
	{ m_hex = hex; }

	virtual std::string type() const override
	{ return H::type(); }

	virtual std::string to_string(unsigned, bool) const override
	{
		if (!m_hex) {
			return std::to_string(m_val);
		} else {
			return "0x" + bcm2dump::to_hex(m_val);
		}
	}

	virtual bool parse(const std::string& str) override
	{
		try {
			m_val = bcm2dump::lexical_cast<T>(str, 0);
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
struct nv_u8_h
{
	static std::string type()
	{ return "u8"; }
};

struct nv_u16_h
{
	static std::string type()
	{ return "u16"; }
};

struct nv_u32_h
{
	static std::string type()
	{ return "u32"; }
};
}

typedef nv_num<uint8_t, detail::nv_u8_h> nv_u8;
typedef nv_num<uint16_t, detail::nv_u16_h> nv_u16;
typedef nv_num<uint32_t, detail::nv_u32_h> nv_u32;

class nv_bool : public nv_u8
{
	public:
	virtual std::string type() const override
	{ return "bool"; }

	virtual bool parse(const std::string& str) override;
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
	virtual std::string type() const override
	{ return "version"; }

	virtual std::string to_string(unsigned, bool) const override
	{ return std::to_string(m_val >> 8) + "." + std::to_string(m_val & 0xff); }

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

	nv_group()
	: nv_compound(true) {}

	nv_group(uint32_t magic)
	: nv_group(nv_magic(magic)) {}

	nv_group(const std::string& magic)
	: nv_group(nv_magic(magic)) {}

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

	bool init(bool force) override;

	protected:
	nv_group(const nv_magic& magic);
	virtual list definition() const override final
	{ return definition(m_type, m_version.major(), m_version.minor()); }
	virtual list definition(int type, int maj, int min) const;
	virtual std::istream& read(std::istream& is) override;

	nv_u16 m_size;
	nv_magic m_magic;
	nv_version m_version;
	int m_type = type_unknown;

	static std::map<nv_magic, csp<nv_group>> s_registry;

};

class nv_group_generic : public nv_group
{
	public:
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

#endif

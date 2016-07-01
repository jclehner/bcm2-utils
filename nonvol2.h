#ifndef BCM2CFG_NONVOL_H
#define BCM2CFG_NONVOL_H
#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include "util.h"

namespace bcm2cfg {

class serializable
{
	public:
	virtual ~serializable() {}
	virtual std::istream& read(std::istream& is) = 0;
	virtual std::ostream& write(std::ostream& os) const = 0;
};

class nv_val : public serializable
{
	public:
	typedef std::shared_ptr<nv_val> sp;
	struct named
	{
		named(const std::string& name, const sp& val)
		: name(name), val(val) {}

		std::string name;
		sp val;
	};
	typedef std::vector<named> list;

	nv_val() : m_set(false) {}

	virtual std::string type() const = 0;
	virtual std::string to_string(bool quote = false) const = 0;
	virtual bool parse(const std::string& str) = 0;

	bool is_set() const
	{ return m_set; }

	// when default-constructed, return the minimum byte count
	// required for this type. after a value has been set, return
	// the number of bytes required to store this value
	virtual size_t bytes() const = 0;

	// for compound types, provide a facility to get/set only a part
	virtual sp get(const std::string& name) const;
	virtual void set(const std::string& name, const std::string& val);

	virtual const list& parts() const
	{ return m_parts; }

	protected:
	virtual void parse_checked(const std::string& str);

	bool m_set;
	list m_parts;
};

class nv_compound : public nv_val
{
	public:
	nv_compound(bool partial, size_t width)
	: nv_compound(partial, width, false) {}

	virtual bool parse(const std::string& str) override;

	virtual sp get(const std::string& name) const override;
	virtual void set(const std::string& name, const std::string& val) override;

	virtual void init(bool force = false);
	virtual void clear()
	{ init(true); }

	virtual size_t bytes() const override
	{ return m_bytes ? m_bytes : m_width; }

	virtual std::istream& read(std::istream& is) override;
	virtual std::ostream& write(std::ostream& os) const override;

	protected:
	nv_compound(bool partial, size_t width, bool internal);
	nv_compound(bool partial)
	: nv_compound(partial, 0, true) {}
	// like get, but shouldn't throw
	virtual sp find(const std::string& name) const;
	virtual list definition() const = 0;

	bool m_partial = false;
	size_t m_width = 0;
	size_t m_bytes = 0;
};


/*
template<class T> class nv_val_base : public nv_val
{
	public:
	typedef T value_type;

	nv_val_base(const T& def) : m_set(false), m_val(def) {}
	nv_val_base() : m_set(false) {}

	virtual const T& get() const
	{ return m_val; }

	virtual void set(const T& t)
	{
		m_val = t;
		m_set = true;
	}

	protected:
	bool m_set;
	T m_val;
};
*/

class nv_data : public nv_val
{
	public:
	explicit nv_data(size_t width);

	virtual std::string type() const override
	{ return "data[" + std::to_string(m_buf.size()) + "]"; }

	virtual std::string to_string(bool quote) const override;

	virtual bool parse(const std::string& str) override
	{ return false; }

	virtual std::istream& read(std::istream& is) override;

	virtual std::ostream& write(std::ostream& os) const override
	{  return os.write(m_buf.data(), m_buf.size()); }

	virtual size_t bytes() const override
	{ return m_buf.size(); }

	bool operator!=(const nv_data& other)
	{ return m_buf == other.m_buf; }

	protected:
	std::string m_buf;

};

class nv_string : public nv_val
{
	public:
	explicit nv_string(size_t width = 0) : m_width(width) {}

	// it doesn't matter to the user whether it's a nv_pstring or a nv_zstring
	virtual std::string type() const override
	{ return "string" + (m_width ? "[" + std::to_string(m_width) + "]" : ""); }

	virtual std::string to_string(bool quote = false) const override
	{ return quote ? '"' + m_val + '"' : m_val; }

	virtual bool parse(const std::string& str) override;

	bool operator!=(const nv_string& other)
	{ return m_val == other.m_val; }

	protected:
	size_t m_width;
	std::string m_val;
};

class nv_zstring : public nv_string
{
	public:
	explicit nv_zstring(size_t width = 0) : nv_string(width) {}

	virtual std::istream& read(std::istream& is) override;
	virtual std::ostream& write(std::ostream& os) const override;

	virtual size_t bytes() const override
	{ return m_width ? m_width : m_val.size() + 1; }
};

class nv_pstring : public nv_string
{
	public:
	explicit nv_pstring(size_t width = 0) : nv_string(width) {}

	virtual bool parse(const std::string& str) override
	{ return str.size() <= 0xffff ? nv_string::parse(str) : false; }

	virtual std::istream& read(std::istream& is) override;
	virtual std::ostream& write(std::ostream& os) const override;

	virtual size_t bytes() const override
	{ return 2 + m_val.size(); }
};

template<class T, class H> class nv_num : public nv_val
{
	public:
	nv_num(bool hex = false) : m_val(0), m_hex(hex) {}
	explicit nv_num(T val, bool hex = false) : m_val(val), m_hex(hex) {}

	virtual void hex(bool hex = true)
	{ m_hex = hex; }

	virtual std::string type() const override
	{ return H::type(); }

	virtual std::string to_string(bool = false) const override
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
			m_val = H::ntoh(m_val);
			m_set = true;
		}

		return is;
	}

	virtual std::ostream& write(std::ostream& os) const override
	{
		T swapped = H::hton(m_val);
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
	static constexpr uint8_t hton(uint8_t val)
	{ return val; }

	static constexpr uint8_t ntoh(uint8_t val)
	{ return val; }

	static std::string type()
	{ return "u8"; }
};

struct nv_u16_h
{
	static uint16_t hton(uint16_t val)
	{ return htons(val); }

	static uint16_t ntoh(uint16_t val)
	{ return ntohs(val); }

	static std::string type()
	{ return "u16"; }
};

struct nv_u32_h
{
	static uint32_t hton(uint32_t val)
	{ return htonl(val); }

	static uint32_t ntoh(uint32_t val)
	{ return ntohl(val); }

	static std::string type()
	{ return "u16"; }
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

	virtual std::string to_string(bool) const override;
};

class nv_version : public nv_u16
{
	public:
	virtual std::string type() const override
	{ return "version"; }

	virtual std::string to_string(bool) const override
	{ return std::to_string(m_val >> 8) + "." + std::to_string(m_val & 0xff); }
};

class nv_group : public nv_compound
{
	public:
	static constexpr int type_unknown = 0;
	static constexpr int type_perm = 1;
	static constexpr int type_dyn = 2;
	static constexpr int type_cfg = 3;

	nv_group()
	: nv_compound(true), m_type(type_unknown) {}

	nv_group(uint32_t magic, int type)
	: nv_group(nv_magic(magic), type) {}

	nv_group(const std::string& magic, int type)
	: nv_group(nv_magic(magic), type) {}

	virtual std::string type() const override
	{ return "group[" + m_magic.to_string(false) + "]"; }

	virtual std::string to_string(bool) const override
	{ throw false; }

	virtual std::istream& read(std::istream& is) override;
	virtual std::ostream& write(std::ostream& os) const override;

	virtual bool is_versioned() const
	{ return true; }

	protected:
	nv_group(const nv_magic& magic, int type);
	virtual list definition() const override;

	nv_u16 m_size;
	nv_magic m_magic;
	nv_version m_version;
	int m_type;
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

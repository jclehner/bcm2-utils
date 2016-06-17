#ifndef BCM2UTILS_UTIL_H
#define BCM2UTILS_UTIL_H
#include <type_traits>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdexcept>
#include <typeinfo>
#include <iostream>
#include <iomanip>
#include <netdb.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

namespace bcm2dump {

std::string trim(std::string str);
std::vector<std::string> split(const std::string& str, char delim, bool empties = true);

inline bool contains(const std::string& haystack, const std::string& needle)
{
	return haystack.find(needle) != std::string::npos;
}

template<class T> T extract(const std::string& data, std::string::size_type offset = 0)
{
	return *reinterpret_cast<const T*>(data.substr(offset, sizeof(data)).c_str());
}

template<class T> void patch(std::string& data, std::string::size_type offset, const T& t)
{
	data.replace(offset, sizeof(T), std::string(reinterpret_cast<const char*>(&t), sizeof(T)));
}

class bad_lexical_cast : public std::invalid_argument
{
	public:
	bad_lexical_cast(const std::string& str) : std::invalid_argument(str) {}
};

template<class T> T lexical_cast(const std::string& str, unsigned base = 10)
{
	std::istringstream istr(str);
	T t;

	if (!base) {
		if (str.size() > 2 && str.substr(0, 2) == "0x") {
			base = 16;
		} else if (str.size() > 1 && str[0] == '0') {
			base = 8;
		} else {
			base = 10;
		}
	}

	if (!(istr >> std::setbase(base) >> t)) {
		throw bad_lexical_cast("conversion failed: '" + str + "' -> " + std::string(typeid(T).name()));
	}

	return t;
}

template<class T> std::string to_hex(const T& t, size_t width = sizeof(T) * 2)
{
	std::ostringstream ostr;
	ostr << std::setfill('0') << std::setw(width) << std::hex << t;
	return ostr.str();
}

std::string to_hex(const std::string& buffer);

// return the closest number lower than num that matches the requested alignment
template<class T> T align_left(const T& num, size_t alignment)
{
	return num - (num % alignment);
}

// return the closest number higher than num that matches the requested alignment
template<class T> T align_right(const T& num, size_t alignment)
{
	T rem = num % alignment;
	return num + (rem ? alignment - rem : 0);
}

uint16_t crc16_ccitt(const void* buf, size_t size);
inline uint16_t crc16_ccitt(const std::string& buf)
{ return crc16_ccitt(buf.data(), buf.size()); }

class logger
{
	public:
	enum severity
	{
		TRACE = 0,
		DEBUG = 1,
		VERBOSE = 2,
		INFO = 3,
		WARN = 4,
		ERR = 5
	};

	static std::ostream& log(severity s);

	static std::ostream& t()
	{ return log(TRACE); }

	static std::ostream& v()
	{ return log(VERBOSE); }

	static std::ostream& d()
	{ return log(DEBUG); }

	static std::ostream& i()
	{ return log(INFO); }

	static std::ostream& w()
	{ return log(WARN); }

	static std::ostream& e()
	{ return log(ERR); }

	static void loglevel(severity s)
	{ s_loglevel = s; }

	private:
	static std::ofstream s_bucket;
	static severity s_loglevel;
};

class getaddrinfo_category : public std::error_category
{
	virtual const char* name() const noexcept override
	{ return "getaddrinfo_category"; };

	virtual std::string message(int condition) const override;
};

class tcpaddrs
{
	public:
	static constexpr int flag_ipv4_only = 1;
	static constexpr int flag_throw = 2;

	tcpaddrs(const tcpaddrs& other) = default;
	tcpaddrs(tcpaddrs&& other) = default;

	~tcpaddrs()
	{
		if (m_result) {
			freeaddrinfo(m_result);
		}
	}

	void rewind()
	{ m_iter = m_result; }

	bool empty() const
	{ return !m_result; }

	addrinfo* get()
	{ return m_iter; }

	addrinfo* next()
	{ return m_iter ? (m_iter = m_iter->ai_next) : nullptr; }

	static tcpaddrs resolve(const std::string& node, int flags = 0);

	private:
	tcpaddrs(addrinfo* result) : m_result(result), m_iter(result) {}

	addrinfo* m_result = nullptr;
	addrinfo* m_iter = nullptr;

};
}

#endif

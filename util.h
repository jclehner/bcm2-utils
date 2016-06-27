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
#include <ios>

namespace bcm2dump {

std::string trim(std::string str);
std::vector<std::string> split(const std::string& str, char delim, bool empties = true);

inline bool contains(const std::string& haystack, const std::string& needle)
{
	return haystack.find(needle) != std::string::npos;
}

inline bool is_bfc_prompt(const std::string& str, const std::string& prompt)
{
	return str.find(prompt + ">") != std::string::npos || str.find(prompt + "/") != std::string::npos;
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

inline unsigned elapsed_millis(std::clock_t start, std::clock_t end = std::clock())
{
	return 1000 * (end - start) / CLOCKS_PER_SEC;
}

class scoped_ios_exceptions
{
	public:
	~scoped_ios_exceptions()
	{
		try {
			m_ios.exceptions(m_saved);
		} catch (...) {}
	}

	static scoped_ios_exceptions failbad(std::ios& ios)
	{ return scoped_ios_exceptions(ios, std::ios::failbit | std::ios::badbit, false); }

	static scoped_ios_exceptions none(std::ios& ios)
	{ return scoped_ios_exceptions(ios, std::ios::goodbit, true); }

	private:
	scoped_ios_exceptions(std::ios& ios, std::ios::iostate except, bool replace)
	: m_ios(ios), m_saved(ios.exceptions())
	{
		ios.exceptions(replace ? except : (m_saved | except));
	}

	std::ios& m_ios;
	std::ios::iostate m_saved;
};

class logger
{
	public:
	static constexpr int trace = 0;
	static constexpr int debug = 1;
	static constexpr int verbose = 2;
	static constexpr int info = 3;
	static constexpr int warn = 4;
	static constexpr int err = 5;

	static std::ostream& log(int severity);

	static std::ostream& t()
	{ return log(trace); }

	static std::ostream& v()
	{ return log(verbose); }

	static std::ostream& d()
	{ return log(debug); }

	static std::ostream& i()
	{ return log(info); }

	static std::ostream& w()
	{ return log(warn); }

	static std::ostream& e()
	{ return log(err); }

	static void loglevel(int level)
	{ s_loglevel = level; }

	static int loglevel()
	{ return s_loglevel; }

	private:
	static std::ofstream s_bucket;
	static int s_loglevel;
};

class getaddrinfo_category : public std::error_category
{
	virtual const char* name() const noexcept override
	{ return "getaddrinfo_category"; };

	virtual std::string message(int condition) const override;
};
}

#endif

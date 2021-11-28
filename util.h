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

#ifndef BCM2UTILS_UTIL_H
#define BCM2UTILS_UTIL_H
#include <type_traits>
#include <system_error>
#include <functional>
#include <stdexcept>
#include <typeinfo>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cstdarg>
#include <chrono>
#include <memory>
#include <cerrno>
#include <vector>
#include <string>
#include <list>
#include <ios>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <ws2def.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#endif

#ifdef BCM2CFG_WINXP
#define inet_ntop(af, src, dst, size) (*dst = '\0')
#define inet_pton(af, src, dst) 0
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (std::extent<decltype(x)>::value)
#endif

namespace bcm2dump {

typedef void (*sigh_type)(int);

#if 0
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define DEF_BSWAP(type, func) \
	template<> type cpu_to_be(type num) \
	{ return num; } \
	template<> type cpu_to_le(type num) \
	{ return func(num); }
#else
#define DEF_BSWAP(type, func) \
	template<> type cpu_to_be(type num) \
	{ return func(num); } \
	template<> type cpu_to_le(type num) \
	{ return num; }
#endif

DEF_BSWAP(uint16_t, __builtin_bswap16)
DEF_BSWAP(uint32_t, __builtin_bswap32)
DEF_BSWAP(uint64_t, __builtin_bswap64)
#endif

std::string trim(std::string str);
std::vector<std::string> split(const std::string& str, char delim, bool empties = true, size_t limit = 0);

inline bool contains(const std::string& haystack, const std::string& needle)
{
	return haystack.find(needle) != std::string::npos;
}

inline bool starts_with(const std::string& haystack, const std::string& needle)
{
	if (haystack.size() < needle.size()) {
		return false;
	} else {
		return haystack.substr(0, needle.size()) == needle;
	}
}

inline bool ends_with(const std::string& haystack, const std::string& needle)
{
	if (haystack.size() < needle.size()) {
		return false;
	} else {
		return haystack.substr(haystack.size() - needle.size()) == needle;
	}
}

template<class T> std::string to_buf(const T& t)
{
	return std::string(reinterpret_cast<const char*>(&t), sizeof(T));
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

template<class T> T lexical_cast(const std::string& str, unsigned base = 10, bool all = true)
{
	static std::istringstream istr;
	istr.clear();
	istr.str(str);
	T t;

	if (!base) {
		if (str.size() > 2 && str.substr(0, 2) == "0x") {
			base = 16;
		} else {
			base = 10;
		}
	}

	if ((istr >> std::setbase(base) >> t)) {
		if (!istr.eof() && base == 10) {
			switch (istr.get()) {
			case 'k':
			case 'K':
				t *= 1024;
				break;
			case 'm':
			case 'M':
				t *= 1024 * 1024;
				break;
			default:
				throw bad_lexical_cast("invalid binary suffix in '" + str + "'");
			}
		}

		int c = istr.get() & 0xff;
		if (!all || (istr.eof() || !c)) {
			return t;
		}
	}

	throw bad_lexical_cast("conversion failed: '" + str + "' -> " + std::string(typeid(T).name()));
}

// without these, istr >> t will read only one char instead of parsing the number

template<> inline int8_t lexical_cast<int8_t>(const std::string& str, unsigned base, bool all)
{ return lexical_cast<int16_t>(str, base, all) & 0xff; }

template<> inline uint8_t lexical_cast<uint8_t>(const std::string& str, unsigned base, bool all)
{ return lexical_cast<uint16_t>(str, base, all) & 0xff; }

template<class T> std::string to_hex(const T& t, size_t width = sizeof(T) * 2)
{
	std::ostringstream ostr;
	ostr << std::setfill('0') << std::setw(width) << std::hex << t;
	return ostr.str();
}

template<> inline std::string to_hex<char>(const char& c, size_t width)
{
	return to_hex(c & 0xff, width);
}

template<> inline std::string to_hex<unsigned char>(const unsigned char& c, size_t width)
{
	return to_hex(c & 0xff, width);
}

std::string to_hex(const std::string& buffer);
std::string from_hex(const std::string& hex);

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

class mstimer
{
	public:
	mstimer() { reset(); }

	auto elapsed() const
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>
			(now() - m_start).count();
	}

	void reset()
	{ m_start = now(); }
	
	private:
	typedef std::chrono::time_point<std::chrono::steady_clock> tpt;

	tpt now() const
	{ return std::chrono::steady_clock::now(); }

	tpt m_start;
};

std::string transform(const std::string& str, std::function<int(int)> f);

template<class T> T ntoh(const T& n);
template<class T> T hton(const T& n);

#define BCM2UTILS_DEF_BSWAPPER(type, f_ntoh, f_hton) \
		template<> inline type ntoh(const type& n) \
		{ return f_ntoh(n); } \
		\
		template<> inline type hton(const type& n) \
		{ return f_hton(n); }

BCM2UTILS_DEF_BSWAPPER(uint8_t,,);
BCM2UTILS_DEF_BSWAPPER(int8_t,,);
BCM2UTILS_DEF_BSWAPPER(uint16_t, ntohs, htons);
BCM2UTILS_DEF_BSWAPPER(int16_t, ntohs, htons);
BCM2UTILS_DEF_BSWAPPER(uint32_t, ntohl ,htonl);
BCM2UTILS_DEF_BSWAPPER(int32_t, ntohl, htonl);

#undef BCM2UTILS_DEF_BSWAPPER

template<typename T> using sp = std::shared_ptr<T>;
template<typename T> using csp = std::shared_ptr<const T>;

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

class cleaner
{
	public:
	cleaner(std::function<void()> init, std::function<void()> cleanup)
	: m_cleanup(cleanup)
	{
		if (init) {
			init();
		}
	}

	cleaner(std::function<void()> cleanup) : cleaner(nullptr, cleanup) {}
	~cleaner()
	{ m_cleanup(); }

	private:
	std::function<void()> m_cleanup;
};

#define BCM2UTILS_LOGF_BODY(severity, format) \
		va_list args; \
		va_start(args, format); \
		log(severity, format, args); \
		va_end(args)

#define BCM2UTILS_DEF_LOGF(name, severity) \
		static void name(const char* format, ...) __attribute__((format(printf, 1, 2))) \
		{ \
			BCM2UTILS_LOGF_BODY(severity, format); \
		}

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

	static void log(int severity, const char* format, va_list args);
	static void log_io(const std::string& line, bool in);

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

	BCM2UTILS_DEF_LOGF(i, info);
	BCM2UTILS_DEF_LOGF(v, verbose);
	BCM2UTILS_DEF_LOGF(d, debug);
	BCM2UTILS_DEF_LOGF(t, trace);

	static void loglevel(int level)
	{ s_loglevel = level; }

	static int loglevel()
	{ return s_loglevel; }

	static void no_stdout(bool no_stdout = true)
	{ s_no_stdout = no_stdout; }

	static void set_logfile(const std::string& filename);

	static std::list<std::string> get_last_io_lines()
	{ return s_lines; }

	private:
	static std::list<std::string> s_lines;
	static int s_loglevel;
	static bool s_no_stdout;
};

class user_error : public std::runtime_error
{
	public:
	user_error(const std::string& what)
	: std::runtime_error(what)
	{}
};

class errno_error : public std::system_error
{
	public:
	errno_error(const std::string& what, int error = errno)
	: std::system_error(error, std::system_category(), what), m_interrupted(error == EINTR)
	{}

	bool interrupted() const noexcept
	{ return m_interrupted; }

	private:
	bool m_interrupted;
};

class getaddrinfo_category : public std::error_category
{
	virtual const char* name() const noexcept override
	{ return "getaddrinfo_category"; };

	virtual std::string message(int condition) const override;
};

#ifdef _WIN32
class winapi_category : public std::error_category
{

	virtual const char* name() const noexcept override
	{ return "winapi_category"; };

	virtual std::string message(int condition) const override;
};

class winapi_error : public std::system_error
{
	public:
	winapi_error(const std::string& what, DWORD error = GetLastError())
	: std::system_error(error, winapi_category(), what)
	{}
};

class winsock_error : public winapi_error
{
	public:
	winsock_error(const std::string& what, DWORD error = WSAGetLastError())
	: winapi_error(what, error) {}
};
#endif
}

template<class T> struct bcm2dump_def_comparison_operators
{
	friend bool operator!=(const T& lhs, const T& rhs)
	{ return !(lhs == rhs); }

	friend bool operator <=(const T& lhs, const T& rhs)
	{ return lhs < rhs || lhs == rhs; }

	friend bool operator>(const T& lhs, const T& rhs)
	{ return !(lhs <= rhs); }

	friend bool operator>=(const T& lhs, const T& rhs)
	{ return !(lhs < rhs); }
};

#endif

#ifndef BCM2UTILS_UTIL_H
#define BCM2UTILS_UTIL_H
#include <stdexcept>
#include <typeinfo>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

namespace bcm2dump {

std::string trim(std::string str);

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
		throw bad_lexical_cast("conversion failed: " + str + " -> " + std::string(typeid(T).name()));
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

class logger
{
	public:
	enum severity
	{
		TRACE = 0,
		DEBUG = 1,
		INFO = 2,
		WARN = 3,
		ERR = 4
	};

	static std::ostream& log(severity s);

	static std::ostream& v()
	{ return log(TRACE); }

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


}

#endif

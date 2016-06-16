#include "profile.h"
#include "util.h"
using namespace std;

namespace bcm2dump {

string trim(string str)
{
	if (str.empty()) {
		return str;
	}

	auto i = str.find_last_not_of(" \x0d\n\t");
	if (i != string::npos) {
		str.erase(i + 1);
	}

	i = str.find_first_not_of(" \x0d\n\t");
	if (i == string::npos) {
		return "";
	}

	return str.substr(i);
}

string to_hex(const std::string& buffer)
{
	string ret;

	for (char c : buffer) {
		ret += to_hex(c);
	}

	return ret;
}

uint16_t crc16_ccitt(const void* buf, size_t size)
{
	uint32_t crc = 0xffff;
	uint32_t poly = 0x1021;

	for (size_t i = 0; i < size; ++i) {
		for (size_t k = 0; k < 8; ++k) {
			bool bit = (reinterpret_cast<const char*>(buf)[i] >> (7 - k) & 1);
			bool c15 = (crc >> 15 & 1);
			crc <<= 1;
			if (c15 ^ bit) {
				crc ^= poly;
			}
		}
	}

	return crc & 0xffff;
}

ofstream logger::s_bucket;
logger::severity logger::s_loglevel = logger::INFO;

ostream& logger::log(severity s)
{
	if (s < s_loglevel) {
		return s_bucket;
	} else if (s >= WARN) {
		return cerr;
	} else {
		return cout;
	}

}



}

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

#include <algorithm>
#include "profile.h"
#include "util.h"
using namespace std;

namespace bcm2dump {
namespace {
string& unescape(string& str, char delim)
{
	string::size_type i = 0;
	while ((i = str.find('\\', i)) != string::npos) {
		if ((i + 1) >= str.size()) {
			throw invalid_argument("stray backslash in '" + str + "'");
		} else if (str[i + 1] == '\\' || str[i + 1] == delim) {
			str.erase(i, 1);
			i += 1;
		} else {
			throw invalid_argument("invalid escape sequence in '" + str + "'");
		}
	}

	return str;
}

void push_back(vector<string>& strings, string str, char delim, bool empties, size_t limit)
{
	if (empties || !str.empty()) {
		if (!limit || strings.size() < limit) {
			strings.push_back(unescape(str, delim));
		} else {
			strings.back() += delim + unescape(str, delim);
		}
	}
}

bool is_unescaped(const string& str, string::size_type pos)
{
	if (!pos) {
		return true;
	}

	bool ret = false;

	if (pos >= 1) {
		ret = str[pos - 1] != '\\';
	}

	if (pos >= 2 && !ret) {
		ret = str[pos - 2] == '\\';
	}

	return ret;
}

// inspired by http://wordaligned.org/articles/cpp-streambufs
//
// we don't care if the operations on the ofstream's buffer fail,
// as this is expected if we're not using a logfile!

class logbuf : public streambuf
{
	public:
	logbuf(ostream& os)
	: m_os(os)
	{}

	static ofstream file;

	protected:
	virtual int overflow(int c) override
	{
		file.rdbuf()->sputc(c);
		return m_os.rdbuf()->sputc(c);
	}

	virtual int sync() override
	{
		file.rdbuf()->pubsync();
		return m_os.rdbuf()->pubsync();
	}

	private:
	ostream& m_os;
};

ofstream logbuf::file;

ostream log_cout(new logbuf(cout));
ostream log_cerr(new logbuf(cerr));
}

string trim(string str)
{
	if (str.empty()) {
		return str;
	}

	auto i = str.find_last_not_of(" \r\n\t");
	if (i != string::npos) {
		str.erase(i + 1);
	}

	i = str.find_first_not_of(" \r\n\t");
	if (i == string::npos) {
		return "";
	}

	str = str.substr(i);

	// strip embedded carriage returns
	while ((i = str.find('\r')) != string::npos) {
		str.erase(i, 1);
	}

	return str;
}

vector<string> split(const string& str, char delim, bool empties, size_t limit)
{
	string::size_type beg = 0, end = str.find(delim);
	vector<string> subs;

	while (end != string::npos) {
		if (is_unescaped(str, end)) {
			push_back(subs, str.substr(beg, end - beg), delim, empties, limit);
			beg = end + 1;
		}

		end = str.find(delim, end + 1);

		if (end == string::npos) {
			push_back(subs, str.substr(beg, str.size()), delim, empties, limit);
		}
	}

	if (subs.empty() && !str.empty()) {
		push_back(subs, str, delim, empties, limit);
	}

	return subs;
}

string to_hex(const std::string& buffer)
{
	string ret;

	for (char c : buffer) {
		ret += to_hex(c & 0xff, 2);
	}

	return ret;
}

string from_hex(const string& hexstr)
{
	if (hexstr.size() % 2) {
		throw user_error("invalid hex-string size "
				+ to_string(hexstr.size()));
	}

	size_t i = 0;

	if (starts_with(hexstr, "0x")) {
		i += 2;
	}

	string ret;
	ret.reserve((hexstr.size() - i) / 2);

	for (; i < hexstr.size(); i += 2) {
		string chr = hexstr.substr(i, 2);
		try {
			ret += lexical_cast<int>(hexstr.substr(i, 2), 16);
		} catch (const bad_lexical_cast& e) {
			throw user_error("invalid hex char '" + chr + "'");
		}
	}

	return ret;
}

std::string transform(const std::string& str, std::function<int(int)> f)
{
	string ret;
	ret.reserve(str.size());

	for (int c : str) {
		ret += f(c);
	}

	return ret;
}

std::string escape(string str, bool escape_quote)
{
	string::size_type pos;
	while ((pos = str.find_first_of(escape_quote ? "\\\"" : "\\")) != string::npos) {
		str.insert(pos, 1, '\'');
	}

	string::iterator it = str.begin();
	while ((it = find_if(it, str.end(), [] (auto c) { return !isprint(c); })) != str.end()) {
		str.replace(it, it + 1, "\\x" + to_hex(*it));
	}

	return str;
}

int logger::s_loglevel = logger::info;
bool logger::s_no_stdout = false;
list<string> logger::s_lines;

constexpr int logger::trace;
constexpr int logger::debug;
constexpr int logger::verbose;
constexpr int logger::info;
constexpr int logger::warn;
constexpr int logger::err;

ostream& logger::log(int severity)
{
	if (severity < s_loglevel) {
		return logbuf::file;
	} else if (s_no_stdout || severity >= warn) {
		return log_cerr;
	} else {
		return log_cout;
	}
}

void logger::log(int severity, const char* format, va_list args)
{
	if (severity < s_loglevel) {
		return;
	}

	char buf[256];
	vsnprintf(buf, sizeof(buf), format, args);
	log(severity) << buf;
}

void logger::log_io(const string& line, bool in)
{
	if (s_lines.size() == 50) {
		s_lines.pop_front();
	}

	s_lines.push_back((in ? "==> " : "<== ") + (line.empty() ?
				"(empty)"s : ("'" + trim(line.c_str()) + "'")));

	ostream& os = logbuf::file ? logbuf::file : log(trace);
	os << s_lines.back() << endl;
}

void logger::set_logfile(const string& filename)
{
	logbuf::file.open(filename.c_str());
}

string getaddrinfo_category::message(int condition) const
{
#ifndef _WIN32
	return gai_strerror(condition);
#else
	return to_string(condition);
#endif
}

#ifdef _WIN32
string winapi_category::message(int condition) const
{
	char* buf = nullptr;
	DWORD len = FormatMessageA(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			condition,
			//MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			0,
			reinterpret_cast<LPTSTR>(&buf),
			0,
			nullptr);

	if (buf && len) {
		string msg = trim(buf);
		LocalFree(buf);
		return msg;
	}

	return "0x" + to_hex(condition);
}
#endif

}

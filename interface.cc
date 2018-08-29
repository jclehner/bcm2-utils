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

#include <sys/stat.h>
#include <algorithm>
#include <unistd.h>
#include <list>
#include "interface.h"
#include "rwx.h"
using namespace std;

namespace bcm2dump {
namespace {

bool is_char_device(const string& filename)
{
	struct stat st;
	errno = 0;
	if (::stat(filename.c_str(), &st) != 0 && errno != ENOENT) {
		throw errno_error("stat('" + filename + "')");
	}

	return !errno ? S_ISCHR(st.st_mode) : false;
}

class telnet
{
	public:
	virtual ~telnet() {}
	virtual bool login(const string& user, const string& pw) = 0;
};

class bfc : public interface
{
	public:
	virtual string name() const override
	{ return "bfc"; }

	virtual bool is_ready(bool passive) override;

	virtual bcm2_interface id() const override
	{ return BCM2_INTF_BFC; }

	virtual void runcmd(const string& cmd) override
	{ writeln(cmd); }
};

bool bfc::is_ready(bool passive)
{
	if (!passive) {
		writeln();
	}

	return foreach_line([] (const string& line) {
		if (is_bfc_prompt(line, "CM")) {
			return true;
		} else if (is_bfc_prompt(line, "Console")) {
			return true;
		}

		return false;
	}, 2000);
}

class bootloader : public interface
{
	public:
	virtual string name() const override
	{ return "bootloader"; }

	virtual bool is_ready(bool passive) override;

	virtual bcm2_interface id() const override
	{ return BCM2_INTF_BLDR; }

	virtual void runcmd(const string& cmd) override;
};

bool bootloader::is_ready(bool passive)
{
	if (!passive) {
		writeln();
	}

	return foreach_line([] (const string& line) {
		return line.find("Main Menu") != string::npos;
	}, 2000);
}

void bootloader::runcmd(const string& cmd)
{
#if 0
	if (cmd.size() != 1) {
		throw invalid_argument("invalid bootloader command: " + cmd);
	}
#endif

	m_io->write(cmd);
}

class bfc_telnet : public bfc, public telnet
{
	public:
	static unsigned constexpr invalid = 0;
	static unsigned constexpr connected = 1;
	static unsigned constexpr authenticated = 2;
	static unsigned constexpr rooted = 3;

	virtual ~bfc_telnet()
	{
		try {
			runcmd("exit");
		} catch (...) {

		}
	}

	virtual void runcmd(const string& cmd) override;
	virtual bool is_active() override
	{ return is_ready(true); }
	virtual bool is_ready(bool passive) override;

	bool login(const string& user, const string& pass) override;

	unsigned status() const
	{ return m_status; }

	protected:
	virtual uint32_t timeout() const override
	{ return 50; }

	private:
	unsigned m_status = invalid;
};

bool bfc_telnet::is_ready(bool passive)
{
	if (m_status < authenticated) {
		if (!passive) {
			writeln();
		}

		foreach_line([this] (const string& line) {
			if (m_status != invalid) {
				return true;
			}

			if (contains(line, "Telnet")) {
				m_status = connected;
			}

			if (m_status == connected && (contains(line, "refused")
					|| contains(line, "logged and reported"))) {
				throw runtime_error("ip is blocked by server");
			}

			return false;

		}, 2000, 500);

		return m_status >= connected;
	} else {
		return bfc::is_ready(passive);
	}
}

void bfc_telnet::runcmd(const string& cmd)
{
	if (m_status < authenticated) {
		throw runtime_error("not authenticated");
	}

	bfc::runcmd(cmd);
}

bool bfc_telnet::login(const string& user, const string& pass)
{
	bool send_crlf = true;

	while (pending(1000)) {
		string line = readln();
		if (contains(line, "Login:") || contains(line, "login:")) {
			send_crlf = false;
			break;
		}
	}

	if (send_crlf) {
		writeln();
	}

	writeln(user);
	while (pending(1000)) {
		string line = readln();
		if (contains(line, "Password:") || contains(line, "password:")) {
			break;
		}
	}

	writeln(pass);
	writeln();

	send_crlf = true;

	while (pending(1000)) {
		string line = readln();
		if (contains(line, "Invalid login")) {
			break;
		} else if (is_bfc_prompt(line, "Console")) {
			m_status = authenticated;
		} else if (is_bfc_prompt(line, "CM")) {
			m_status = rooted;

			if (send_crlf) {
				// in some cases, after a telnet login, the prompt displays
				// CM/Console>, but hitting enter switches to Console>, meaning
				// we're NOT rooted.
				writeln();
				send_crlf = false;
			}
		}
	}

	if (m_status == authenticated) {
		runcmd("su");
		foreach_line([this] (const string& line) {
			if (contains(line, "Password:")) {
				writeln("brcm");
				writeln();
			} else if (is_bfc_prompt(line, "CM")) {
				m_status = rooted;
			}

			return false;
		});
	}

	if (m_status == authenticated) {
		logger::w() << "failed to switch to super-user; some functions might not work" << endl;
	}

	return m_status >= authenticated;

}

interface::sp detect_interface(const io::sp &io)
{
	interface::sp intf = make_shared<bfc_telnet>();
	if (intf->is_active(io)) {
		return intf;
	}

	intf = make_shared<bootloader>();
	if (intf->is_active(io)) {
		return intf;
	}

	intf = make_shared<bfc>();
	if (intf->is_active(io)) {
		return intf;
	}

	throw runtime_error("interface auto-detection failed");
}

void detect_profile_if_not_set(const interface::sp& intf, const profile::sp& profile)
{
	if (profile) {
		// TODO allow manually specifying a version, auto-detect otherwise
		intf->set_profile(profile);
		return;
	}

	rwx::sp ram = rwx::create(intf, "ram", true);

	// sort all magic specifications, and try them in ascending order. this
	// is to avoid crashing a device by trying an offset that is outside its
	// valid range.

	struct comp
	{
		bool operator()(const bcm2_magic* m1, const bcm2_magic* m2) const
		{
			return m1->addr < m2->addr;
		}
	};

	multimap<const bcm2_magic*, pair<profile::sp, version>, comp> magics;

	for (auto p : profile::list()) {
		for (auto v : p->versions()) {
			if (v.intf() == intf->id()) {
				magics.insert(make_pair(v.magic(), make_pair(p, v)));
			}
		}

		for (auto m : p->magics()) {
			magics.insert(make_pair(m, make_pair(p, version())));
		}
	}

	for (int pass = 0; pass < 2; ++pass) {
		for (const auto& kv : magics) {
			// try specific versions in the first pass, and general
			// magic strings in the second pass.
			//
			// TODO: detect device first, then specific version?

			auto v = kv.second.second;
			if (pass == 0 && v.name().empty()) {
				continue;
			}

			string data = kv.first->data;
			if (ram->read(kv.first->addr, data.size()) == data) {

				auto p = kv.second.first;
				logger::i() << "detected profile " << p->name() << "(" << intf->name() << ")";

				if (!v.name().empty()) {
					logger::i() << ", version " << v.name();
				} else {
					v = p->default_version(intf->id());
				}

				logger::i() << endl;
				intf->set_profile(p, v);
				return;
			}
		}
	}

	logger::i() << "profile auto-detection failed" << endl;
}
}

bool interface::wait_ready(unsigned timeout)
{
	time_t begin = time(NULL);

	while ((time(nullptr) - begin) < timeout) {
		if (is_ready()) {
			return true;
		}
		usleep(10000);
	}

	return false;
}

bool interface::runcmd(const string& cmd, const string& expect, bool stop_on_match)
{
	runcmd(cmd);
	bool match = false;

	while (pending()) {
		string line = readln();
		if (line.empty()) {
			break;
		}

		if (line.find(expect) != string::npos) {
			match = true;
			if (stop_on_match) {
				break;
			}
		}
	}

	return match;
}

bool interface::foreach_line(function<bool(const string&)> f, unsigned timeout, unsigned timeout_line) const
{
	clock_t start = clock();

	while (pending(timeout_line) && (!timeout || elapsed_millis(start) < timeout)) {
		string line = readln();
		if (line.empty()) {
			break;
		}

		if (f(line)) {
			return true;
		}
	}

	return false;
}

interface::sp interface::detect(const io::sp& io, const profile::sp& profile)
{
	interface::sp intf = detect_interface(io);
	detect_profile_if_not_set(intf, profile);
	return intf;
}

interface::sp interface::create(const string& spec, const string& profile_name)
{
	profile::sp profile;
	if (!profile_name.empty()) {
		profile = profile::get(profile_name);
	}

	string type;
	vector<string> tokens = split(spec, ':', false);
	if (tokens.size() == 2) {
		type = tokens[0];
		tokens.erase(tokens.begin());
	}

	tokens = split(tokens[0], ',', true);

	if (type.empty()) {
		if (tokens.size() == 1 || (tokens.size() == 2 && is_char_device(tokens[0]))) {
			type = "serial";
		} else if (tokens.size() == 2 && !is_char_device(tokens[0])) {
			type = "tcp";
		} else if (tokens.size() == 3 || tokens.size() == 4) {
			type = "telnet";
		} else {
			throw invalid_argument("ambiguous interface: '" + spec + "'; use <type>: prefix (serial/tcp/telnet)");
		}
	}

	try {
		if (type == "serial") {
			unsigned speed = tokens.size() == 2 ? lexical_cast<unsigned>(tokens[1]) : 115200;
			return detect(io::open_serial(tokens[0].c_str(), speed), profile);
		} else if (type == "tcp") {
			return detect(io::open_tcp(tokens[0], lexical_cast<uint16_t>(tokens[1])), profile);
		} else if (type == "telnet") {
			uint16_t port = tokens.size() == 4 ? lexical_cast<uint16_t>(tokens[3]) : 23;
			interface::sp intf = detect_interface(io::open_telnet(tokens[0], port));

			// this is UGLY, but it should never fail
			telnet* t = dynamic_cast<telnet*>(intf.get());
			if (t) {
				if (!t->login(tokens[1], tokens[2])) {
					throw runtime_error("telnet login failed");
				}
			} else {
				logger::w() << "detected non-telnet interface" << endl;
			}

			detect_profile_if_not_set(intf, profile);
			return intf;
		}
	} catch (const bad_lexical_cast& e) {
		throw invalid_argument("invalid " + type + " interface: " + e.what());
	} catch (const exception& e) {
		throw invalid_argument(type + ": " + e.what());
	}

	throw invalid_argument("invalid interface: '" + spec + '"');
}
}

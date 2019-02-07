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
#include <set>
#include "interface.h"
#include "rwx.h"
using namespace std;

namespace bcm2dump {
namespace {

bool is_bfc_prompt(const string& str, const string& prompt)
{
	return str.find(prompt + ">") != std::string::npos
			|| str.find(prompt + "/") != std::string::npos;
}

bool is_bfc_prompt_privileged(const string& str)
{
	return is_bfc_prompt(str, "CM")
		|| is_bfc_prompt(str, "RG");
}

bool is_bfc_prompt_unprivileged(const string& str)
{
	return is_bfc_prompt(str, "RG_Console")
		|| is_bfc_prompt(str, "CM_Console")
		|| is_bfc_prompt(str, "Console");
}

bool is_bfc_prompt_rg(const string& str)
{
	return is_bfc_prompt(str, "RG_Console") || is_bfc_prompt(str, "RG");
}

bool is_bfc_prompt(const string& str)
{
	return is_bfc_prompt_privileged(str) || is_bfc_prompt_unprivileged(str);
}

bool is_char_device(const string& filename)
{
	struct stat st;
	errno = 0;
	if (::stat(filename.c_str(), &st) != 0 && errno != ENOENT) {
		throw errno_error("stat('" + filename + "')");
	}

	return !errno ? S_ISCHR(st.st_mode) : false;
}

uint32_t get_max_magic_addr(const profile::sp& p, int intf_id)
{
	uint32_t ret = 0;

	for (auto v : p->versions()) {
		if (v.intf() == intf_id) {
			ret = max(ret, v.magic()->addr + magic_size(v.magic()) - 1);
		}
	}

	for (auto m : p->magics()) {
		ret = max(ret, m->addr + magic_size(m) - 1);
	}

	return ret;
}

class telnet
{
	public:
	virtual ~telnet() {}
	virtual bool login(const string& user, const string& pw) = 0;
};

class bfc : public interface, public enable_shared_from_this<bfc>
{
	public:
	virtual string name() const override
	{ return "bfc"; }

	virtual bool is_ready(bool passive) override;

	virtual bcm2_interface id() const override
	{ return BCM2_INTF_BFC; }

	virtual void runcmd(const string& cmd) override
	{ writeln(cmd); }

	virtual void elevate_privileges() override;

	virtual bool is_privileged() const override
	{ return m_privileged; }

	protected:
	virtual bool check_privileged();

	private:
	void do_elevate_privileges();
	bool m_privileged = false;
	bool m_is_rg_prompt = false;
};

bool bfc::is_ready(bool passive)
{
	if (!passive) {
		writeln();
	}

	return foreach_line([this] (const string& line) {
		if (is_bfc_prompt(line)) {
			m_privileged = is_bfc_prompt_privileged(line);
			return true;
		}

		return false;
	}, 2000);
}

void bfc::elevate_privileges()
{
	do_elevate_privileges();

	if (!m_privileged) {
		logger::w() << "failed to switch to super-user; some functions might not work" << endl;
	}
}

void bfc::do_elevate_privileges()
{
	if (!m_privileged) {
		check_privileged();
	}

	if (m_is_rg_prompt) {
		runcmd("switchCpuConsole");
		sleep(1);
		writeln();
		m_is_rg_prompt = false;
	}

	if (m_privileged) {
		return;
	}

	runcmd("su");
	usleep(200000);
	writeln(m_version.get_opt_str("bfc:su_password", "brcm"));
	writeln();

	if (check_privileged()) {
		return;
	}

	uint32_t ct_instance = m_version.get_opt_num("bfc:conthread_instance", 0);
	uint32_t ct_priv_off = m_version.get_opt_num("bfc:conthread_priv_off", 0);

	if (ct_instance && ct_priv_off) {
		rwx::sp ram = rwx::create(shared_from_this(), "ram");

		try {
			wait_ready();
			ram->space().check_offset(ct_instance, "bfc:conthread_instance");
			uint32_t addr = ntoh(extract<uint32_t>(ram->read(ct_instance, 4)));
			addr += ct_priv_off;
			ram->space().check_offset(addr, "console_priv_flag");
			ram->write(addr, "\x01"s);
		} catch (const exception& e) {
			logger::d() << "while writing to console thread instance: " << e.what() << endl;
		}

		writeln();
	}

	check_privileged();
}

bool bfc::check_privileged()
{
	foreach_line([this] (const string& l) {
		if (is_bfc_prompt_privileged(l)) {
			m_privileged = true;
		} else if (is_bfc_prompt_unprivileged(l)) {
			m_privileged = false;
		}

		m_is_rg_prompt = is_bfc_prompt_rg(l);

		return false;
	}, 0, 1000);

	return m_privileged;
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

	virtual void elevate_privileges() override;

	protected:
	virtual uint32_t timeout() const override
	{ return 50; }

	private:
	unsigned m_status = invalid;
	bool m_have_login_prompt = false;

	static bool is_login_prompt(const string& line)
	{
		return contains(line, "Login:") || contains(line, "login:");
	}
};

bool bfc_telnet::is_ready(bool passive)
{
	if (m_status < authenticated) {
		if (!passive) {
			writeln();
		}

		foreach_line([this] (const string& line) {
			if (contains(line, "BFC Telnet")) {
				m_status = connected;
			} else if (m_status == connected) {
				if (contains(line, "refused") || contains(line, "logged and reported")) {
					throw user_error("ip is blocked by server");
				} else if (is_login_prompt(line)) {
					m_have_login_prompt = true;
					return true;
				}
			}

			return false;

		}, 0, 1000);

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
	bool have_prompt = m_have_login_prompt;
	bool send_newline = true;

	while (!have_prompt) {
		have_prompt = foreach_line([] (const string& line) {
			return is_login_prompt(line);
		}, 0, 1000);

		if (!have_prompt) {
			if (send_newline) {
				writeln();
				send_newline = false;
			} else {
				logger::d() << "telnet: no login prompt" << endl;
				return false;
			}
		}
	}

	writeln(user);

	have_prompt = foreach_line([] (const string& line) {
		if (contains(line, "Password:") || contains(line, "password:")) {
			return true;
		}

		return false;
	}, 0, 1000);

	if (!have_prompt) {
		logger::d() << "telnet: no password prompt" << endl;
		return false;
	}


	writeln(pass);
	writeln();

	foreach_line([this, &send_newline] (const string& line) {
		if (contains(line, "Invalid login")) {
			return true;
		} else if (is_bfc_prompt(line)) {
			m_status = authenticated;
		}

		return false;
	}, 0, 1000);

	if (m_status == authenticated) {
		writeln();
		writeln();
		check_privileged();
		return true;
	}

	return false;
}

void bfc_telnet::elevate_privileges()
{
	if (m_status != authenticated) {
		return;
	}

	bfc::elevate_privileges();
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

	struct helper
	{
		const bcm2_magic* m;
		const profile::sp p;
		const version v;
		const uint32_t x;
	};

	struct comp
	{
		bool operator()(const helper& a, const helper& b) const
		{
			if (a.p->name() == b.p->name()) {
				if (a.v.name().empty() != b.v.name().empty()) {
					return b.v.name().empty();
				}

				if (a.m->addr == b.m->addr) {
					if (magic_size(a.m) == magic_size(b.m)) {
						return a.v.name() < b.v.name();
					}

					// try the longer magic value first
					return magic_size(a.m) > magic_size(b.m);
				}

				return a.m->addr < b.m->addr;
			}

			return a.x < b.x;
		}
	};

	set<helper, comp> magics;

	for (auto p : profile::list()) {
		uint32_t x = get_max_magic_addr(p, intf->id());

		for (auto v : p->versions()) {
			if (v.intf() == intf->id()) {
				magics.insert({ v.magic(), p, v, x });
			}
		}

		for (auto m : p->magics()) {
			magics.insert({ m, p, version(), x });
		}
	}

	for (const helper& h : magics) {
		string data = magic_data(h.m);
		if (ram->read(h.m->addr, data.size()) == data) {

			logger::i() << "detected profile " << h.p->name() << "(" << intf->name() << ")";
			version v = h.v;

			if (!v.name().empty()) {
				logger::i() << ", version " << v.name();
			} else {
				v = h.p->default_version(intf->id());
			}

			logger::i() << endl;
			intf->set_profile(h.p, v);
			return;
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

	foreach_line([&expect, &stop_on_match, &match] (const string& line) -> bool {
		if (line.find(expect) != string::npos) {
			match = true;
			if (stop_on_match) {
				return true;
			}
		}

		return false;
	});

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
	intf->elevate_privileges();
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
			intf->elevate_privileges();
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

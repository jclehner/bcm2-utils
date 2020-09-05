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

bool is_bfc_login_prompt(const string& line)
{
	return contains(line, "Login:") || contains(line, "login:")
		|| contains(line, "Username:") || contains(line, "username:");
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

set<string> get_all_su_passwords()
{
	set<string> ret;

	for (auto p : profile::list()) {
		for (auto v : p->versions()) {
			if (v.has_opt("bfc:su_password")) {
				ret.insert(v.get_opt_str("bfc:su_password"));
			}
		}
	}

	return ret;
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

	virtual void elevate_privileges() override;

	virtual bool is_privileged() const override
	{ return m_privileged; }

	protected:
	virtual bool check_privileged();
	virtual void detect_profile() override;
	virtual void initialize_impl() override;
	virtual bool is_crash_line(const string& line) const override;
	virtual bool check_for_prompt(const string& line) const override;

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

	return foreach_line_raw([this] (const string& line) {
		if (is_bfc_prompt(line)) {
			m_privileged = is_bfc_prompt_privileged(line);
			return true;
		}

		return false;
	});
}

bool bfc::check_for_prompt(const string& line) const
{
	return is_bfc_prompt(line);
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
		if (m_privileged) {
			// switchCpuConsole isn't available in the root shell!
			run("/exit");
			m_privileged = false;
		}

		run("switchCpuConsole");
		sleep(1);
		writeln();
		m_is_rg_prompt = false;
	}

	if (m_privileged) {
		return;
	}

	set<string> passwords;

	if (m_version.has_opt("bfc:su_password")) {
		passwords.insert(m_version.get_opt_str("bfc:su_password"));
	} else {
		passwords = get_all_su_passwords();
	}

	for (auto pw : passwords) {
		run("su", "Password:", true);
		writeln(pw);
		writeln();

		if (check_privileged()) {
			if (passwords.size() > 1) {
				logger::v() << "su password is '" << pw << "'" << endl;
			}

			return;
		}
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
	foreach_line_raw([this] (const string& l) {
		if (is_bfc_prompt_privileged(l)) {
			m_privileged = true;
		} else if (is_bfc_prompt_unprivileged(l)) {
			m_privileged = false;
		}

		m_is_rg_prompt = is_bfc_prompt_rg(l);

		return false;
	});

	return m_privileged;
}

void bfc::detect_profile()
{
	uint16_t pssig = 0;

	if (is_privileged()) {
		writeln("/version");
	} else {
		writeln("/show version");
	}

	foreach_line([this, &pssig] (const string& l) {
		const string needle = "PID=0x";
		auto pos = l.find(needle);
		if (pos != string::npos) {
			try {
				pssig = lexical_cast<uint16_t>(l.substr(pos + needle.size()), 16, false);
				return true;
			} catch (const exception& e) {
				logger::d() << e.what() << endl;
				// ignore
			}
		}

		return false;
	});

	if (!pssig) {
		return;
	}

	for (auto p : profile::list()) {
		if (p->pssig() == pssig) {
			m_profile = p;
			break;
		}
	}
}

void bfc::initialize_impl()
{
	//run("/docsis/scan_stop");
}

bool bfc::is_crash_line(const string& line) const
{
	return starts_with(line, "******************** CRASH")
		|| starts_with(line, ">>> YIKES... ");
}

class bootloader : public interface
{
	public:
	virtual string name() const override
	{ return "bootloader"; }

	virtual bool is_ready(bool passive) override;

	virtual bcm2_interface id() const override
	{ return BCM2_INTF_BLDR; }

	protected:
	virtual void call(const string& cmd) override;
	virtual bool is_crash_line(const string& line) const override;
	virtual bool check_for_prompt(const string& line) const override;
};

bool bootloader::is_ready(bool passive)
{
	if (!passive) {
		writeln();
	}

	return foreach_line_raw([this] (const string& line) {
		return check_for_prompt(line);	
	}, 1000);
}

bool bootloader::check_for_prompt(const string& line) const
{
	if (!starts_with(line, "Main Menu")) {
		return false;
	}

	foreach_line([] (const string&) { return false; });
	return true;
}

void bootloader::call(const string& cmd)
{
	m_io->write(cmd);
}


bool bootloader::is_crash_line(const string& line) const
{
	return starts_with(line, "******************** CRASH");
}

#if 0
class bootloader2 : public interface
{
	public:
	virtual string name() const override
	{ return "bootloader2"; }

	virtual bool is_ready(bool passive) override;

	virtual bcm2_interface id() const override
	{ return BCM2_INTF_BLDR; }
};

bool bootloader2::is_ready(bool passive)
{
	if (!passive) {
		writeln();
	}

	return foreach_line([] (const string& line) {
		return line.find("> ") == 0;
	}, 2000);
}
#endif

class bfc_telnet : public bfc, public telnet
{
	public:
	static unsigned constexpr invalid = 0;
	static unsigned constexpr connected = 1;
	static unsigned constexpr authenticated = 2;

	virtual ~bfc_telnet()
	{
		try {
			call("/exit");
		} catch (...) {

		}
	}

	virtual bool is_active() override
	{ return is_ready(true); }
	virtual bool is_ready(bool passive) override;

	bool login(const string& user, const string& pass) override;

	virtual void elevate_privileges() override;

	protected:
	virtual uint32_t timeout() const override
	{ return 500; }

	virtual void call(const string& cmd) override;

	private:
	unsigned m_status = invalid;
	bool m_have_login_prompt = false;
};

bool bfc_telnet::is_ready(bool passive)
{
	if (m_status < authenticated) {
		if (!passive) {
			writeln();
		}

		foreach_line_raw([this] (const string& line) {
			if (contains(line, "Telnet Server")) {
				m_status = connected;
			} else if (m_status == connected) {
				if (contains(line, "refused") || contains(line, "logged and reported")) {
					throw user_error("ip is blocked by server");
				} else if (is_bfc_login_prompt(line)) {
					m_have_login_prompt = true;
					return true;
				}
			}

			return false;

		}, 1000);

		return m_status >= connected;
	} else {
		return bfc::is_ready(passive);
	}
}

void bfc_telnet::call(const string& cmd)
{
	if (m_status < authenticated) {
		throw runtime_error("not authenticated");
	}

	bfc::call(cmd);
}

bool bfc_telnet::login(const string& user, const string& pass)
{
	bool have_prompt = m_have_login_prompt;
	bool send_newline = true;

	while (!have_prompt) {
		have_prompt = foreach_line_raw([] (const string& line) {
			return is_bfc_login_prompt(line);
		}, 3000);

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

	have_prompt = foreach_line_raw([] (const string& line) {
		if (contains(line, "Password:") || contains(line, "password:")) {
			return true;
		}

		return false;
	}, 3000);

	if (!have_prompt) {
		logger::d() << "telnet: no password prompt" << endl;
		return false;
	}


	writeln(pass);
	writeln();

	foreach_line_raw([this] (const string& line) {
		if (contains(line, "Invalid login")) {
			return true;
		} else if (is_bfc_prompt(line)) {
			m_status = authenticated;
		}

		return false;
	}, 3000);

	if (m_status == authenticated) {
		// in some cases, the shell prompt is CM/Console>, but
		// switches to Console> after hitting enter, meaning that
		// we're NOT rooted!
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

interface::sp do_detect_interface(const io::sp &io)
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

interface::sp detect_interface(const io::sp &io)
{
	auto intf = do_detect_interface(io);
	logger::d() << "detected interface: " << intf->name() << endl;
	return intf;
}


void detect_profile_from_magics(const interface::sp& intf, const profile::sp& profile)
{
	if (profile) {
		// TODO allow manually specifying a version, auto-detect otherwise
		auto v = profile->default_version(intf->id());
		intf->set_profile(profile, v);
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
		if (profile && profile->name() != p->name()) {
			continue;
		}

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
			version v = h.v;

			if (v.name().empty()) {
				v = h.p->default_version(intf->id());
			}

			intf->set_profile(h.p, v);
			return;
		}
	}
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

bool interface::run(const string& cmd, const string& expect, bool stop_on_match)
{
	call(cmd);
	bool match = false;

	foreach_line_raw([&expect, &stop_on_match, &match] (const string& line) {
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

vector<string> interface::run(const string& cmd, unsigned timeout)
{
	call(cmd);
	vector<string> lines;

	foreach_line([&lines] (const string& line) {
		lines.push_back(line);
		return false;
	}, timeout);

	return lines;
}

bool interface::foreach_line_raw(function<bool(const string&)> f, unsigned timeout) const
{
	mstimer t;

	while (true) {
		string line;

		if (timeout) {
			auto remaining = timeout - t.elapsed();
			if (remaining < 0) {
				break;
			}

			line = readln(remaining);
		} else {
			line = readln();
		}

		if (line.empty()) {
			break;
		} else if (f(line)) {
			return true;
		}
	}

	return false;
}

bool interface::foreach_line(function<bool(const string&)> f, unsigned timeout) const
{
	bool prompt = false;
	bool stopped = foreach_line_raw([this, &prompt, &f] (const string& line) {
		if (check_for_prompt(line)) {
			prompt = true;
			return true;
		}

		return f(line);
	}, timeout);

	// we bailed out using `return true` in case of a prompt, but we don't want to
	// give the impression that `f` returned true!

	return stopped ? (!prompt) : false;
}

string interface::readln(unsigned timeout) const
{
	string line = m_io->readln(timeout ? timeout : this->timeout());

	if (is_crash_line(line)) {
		// consume lines to fill the io log
		foreach_line([] (const string&) { return false; }, 1000);

		throw runtime_error("target has crashed");
	}

	return line;
}

void interface::initialize(const profile::sp& profile)
{
	m_profile = profile;

	initialize_impl();

	if (!m_profile) {
		detect_profile_from_magics(shared_from_this(), m_profile);
		elevate_privileges();
	}

	if (!m_profile) {
		detect_profile();
	}

	if (!m_profile) {
		logger::i() << "profile auto-detection failed" << endl;
	} else {
		logger::i() << "detected profile " << m_profile->name() << "(" << name() << ")";
		if (!m_version.name().empty()) {
			logger::i() << ", version " << m_version.name();
		}
		logger::i() << endl;
	}
}

interface::sp interface::detect(const io::sp& io, const profile::sp& profile)
{
	interface::sp intf = detect_interface(io);
	intf->initialize(profile);
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

			intf->initialize(profile);
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

#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include "interface.h"
#include "rwx.h"
using namespace std;

namespace bcm2dump {
namespace {

typedef runtime_error user_error;

bool is_prompt(const string& str, const string& prompt)
{
	return str.find(prompt + ">") == 0 || str.find(prompt + "/") == 0;
}

bool is_char_device(const string& filename)
{
	struct stat st;
	errno = 0;
	if (::stat(filename.c_str(), &st) != 0 && errno != ENOENT) {
		throw system_error(errno, system_category(), "stat('" + filename + "')");
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

	bool ret = false;

	while (pending()) {
		string line = readln();
		if (line.empty()) {
			break;
		}

		if (is_prompt(line, "CM")) {
			ret = true;
		} else if (is_prompt(line, "Console")) {
			// so we don't have to implement bfc_telnet::is_ready
			ret = true;
		}
	}

	return ret;
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

	bool ret = false;

	while (pending()) {
		string line = readln();
		if (line.empty()) {
			break;
		}

		if (line.find("Main Menu") != string::npos) {
			ret = true;
		}
	}

	return ret;
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
		if (m_status >= authenticated) {
			runcmd("exit");
		}
	}

	virtual void runcmd(const string& cmd) override;
	virtual bool is_active() override
	{ return is_ready(true); }
	virtual bool is_ready(bool passive) override;

	bool login(const string& user, const string& pass) override;

	unsigned status() const
	{ return m_status; }

	private:
	unsigned m_status = invalid;
};

bool bfc_telnet::is_ready(bool passive)
{
	if (m_status < authenticated) {
		if (!passive) {
			writeln();
		}

		while (pending()) {
			string line = readln();

			if (contains(line, "Telnet")) {
				m_status = connected;
			}

			if (m_status == connected && (contains(line, "refused")
					|| contains(line, "logged and reported"))) {
				throw runtime_error("connection refused");
			}
		}

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
	while (pending()) {
		if (contains(readln(), "Login:")) {
			break;
		}
	}

	writeln(user);
	while (pending()) {
		if (contains(readln(), "Password:")) {
			break;
		}
	}

	writeln(pass);
	while (pending()) {
		string line = readln();

		if (contains(line, "Invalid login")) {
			break;
		} else if (contains(line, "Console>")) {
			m_status = authenticated;
		} else if (contains(line, "CM>")) {
			m_status = rooted;
		} else {
			logger::v() << "login: " << line << endl;
		}
	}

	if (m_status == authenticated) {
		runcmd("su");
		writeln("brcm");
		runcmd("cd /");
		while (pending()) {
			if (contains(readln(), "CM>")) {
				m_status = rooted;
			}
		}
	}

	if (m_status == authenticated) {
		logger::w() << "login succeeded, but root failed. some functions might not work" << endl;
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

	throw user_error("interface auto-detection failed");
}

void detect_profile(const interface::sp& intf)
{
	rwx::sp ram = rwx::create(intf, "ram", true);

	for (auto p : profile::list()) {
		for (auto magic : p->magics()) {
			string data = magic->data;
			if (ram->read(magic->addr, data.size()) == data) {
				intf->set_profile(p);
				logger::i() << "detected profile " << p->name() << endl;
				return;
			}
		}
	}

	logger::i() << "profile auto-detection failed" << endl;
}
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

interface::sp interface::detect(const io::sp& io)
{
	interface::sp intf = detect_interface(io);
	detect_profile(intf);
	return intf;
}

interface::sp interface::create(const string& spec)
{
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
			throw invalid_argument("ambiguous interface: '" + spec + '"');
		}
	}

	try {
		if (type == "serial") {
			unsigned speed = tokens.size() == 2 ? lexical_cast<unsigned>(tokens[1]) : 115200;
			return detect(io::open_serial(tokens[0].c_str(), speed));
		} else if (type == "tcp") {
			return detect(io::open_tcp(tokens[0], lexical_cast<uint16_t>(tokens[1])));
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

			detect_profile(intf);
			return intf;
		}
	} catch (const bad_lexical_cast& e) {
		throw invalid_argument("invalid " + type + " interface: " + e.what());
	} catch (const exception& e) {
		throw invalid_argument(type + ": " + e.what());
	}

	throw invalid_argument("invalid interface: '" + spec + '"');
}

unsigned rwx_writer::s_count = 0;
volatile sig_atomic_t rwx_writer::s_sigint = 0;

void rwx_writer::do_cleanup()
{
	if (m_inited) {
		cleanup();
		m_inited = false;
	}
}

void rwx_writer::do_init(uint32_t offset, uint32_t length)
{
	init(offset, length);
	m_inited = true;
	++s_count;
	logger::d() << "installing signal handler" << endl;
	signal(SIGINT, &rwx_writer::handle_signal);
}
}

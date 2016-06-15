#include "interface.h"
#include "reader.h"
using namespace std;

namespace bcm2dump {
namespace {

typedef runtime_error user_error;

bool is_prompt(const string& str, const string& prompt)
{
	return str.find(prompt + ">") == 0 || str.find(prompt + "/") == 0;
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
	reader::sp ram = reader::create(intf, "ram", true);

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

interface::sp interface::create_serial(const string& tty, unsigned speed)
{
	return detect(io::open_serial(tty.c_str(), speed));
}

interface::sp interface::create_telnet(const string& addr, uint16_t port,
		const string& user, const string& pw, const profile::sp& profile)
{
	io::sp io = io::open_telnet(addr, port);
	interface::sp intf = detect_interface(io);
	intf->set_profile(profile);

	// this is UGLY, but it should never fail
	telnet* t = dynamic_cast<telnet*>(intf.get());
	if (t) {
		if (!t->login(user, pw)) {
			throw runtime_error("telnet login failed");
		}
	} else {
		logger::w() << "detected non-telnet interface" << endl;
	}

	return intf;
}

unsigned reader_writer::s_count = 0;
volatile sig_atomic_t reader_writer::s_sigint = 0;

void reader_writer::do_cleanup()
{
	if (m_inited) {
		cleanup();
		m_inited = false;
	}
}

void reader_writer::do_init(uint32_t offset, uint32_t length)
{
	init(offset, length);
	m_inited = true;
	++s_count;
	logger::d() << "installing signal handler" << endl;
	signal(SIGINT, &reader_writer::handle_signal);
}
}

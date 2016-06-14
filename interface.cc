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

class bfc : public interface
{
	public:
	virtual string name() const override
	{ return "bfc"; }

	virtual bool is_active() override;

	virtual bcm2_interface id() const override
	{ return BCM2_INTF_BFC; }

	protected:
	virtual void runcmd(const string& cmd) override
	{ m_io->writeln(cmd); }
};

bool bfc::is_active()
{
	bool ret = false;
	writeln();

	while (pending()) {
		string line = readln();
		if (line.empty()) {
			break;
		}

		if (is_prompt(line, "CM")) {
			ret = true;
		} else if (is_prompt(line, "Console")) {
			// FIXME
			throw user_error("telnet console is not yet supported");
		}
	}

	return ret;
}

class bootloader : public interface
{
	public:
	virtual string name() const override
	{ return "bootloader"; }

	virtual bool is_active() override;

	virtual bcm2_interface id() const override
	{ return BCM2_INTF_BLDR; }

	protected:
	virtual void runcmd(const string& cmd) override;
};

bool bootloader::is_active()
{
	bool ret = false;
	writeln();

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

interface::sp detect_interface(const io::sp &io)
{
	interface::sp intf = make_shared<bootloader>();
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
	reader::sp ram = reader::create(intf, "ram");

	for (auto p : profile::list()) {
		for (auto magic : p->magics()) {
			string data = magic->data;
			if (ram->read(magic->addr, data.size()) == data) {
				intf->set_profile(p);
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

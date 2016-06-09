#include "interface.h"
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
	if (cmd.size() != 1) {
		throw invalid_argument("invalid bootloader command: " + cmd);
	}

	m_io->write(cmd);
}
}

shared_ptr<interface> interface::detect(const shared_ptr<io>& io)
{
	shared_ptr<interface> intf = make_shared<bootloader>();
	if (intf->is_active(io)) {
		return intf;
	}

	intf = make_shared<bfc>();
	if (intf->is_active(io)) {
		return intf;
	}

	throw user_error("interface auto-detection failed");
}
}

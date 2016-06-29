#ifndef BCM2DUMP_INTERFACE_H
#define BCM2DUMP_INTERFACE_H
#include <functional>
#include <csignal>
#include <memory>
#include <string>
#include <map>
#include "profile.h"
#include "util.h"
#include "io.h"

namespace bcm2dump {

class interface
{
	typedef bcm2dump::profile profile_type;

	public:
	typedef std::shared_ptr<interface> sp;

	virtual ~interface() {}

	virtual std::string name() const = 0;
	virtual void runcmd(const std::string& cmd) = 0;
	virtual bool runcmd(const std::string& cmd, const std::string& expect, bool stop_on_match = false);

	virtual void set_profile(const profile::sp& profile)
	{ m_profile = profile; }

	virtual profile_type::sp profile() const
	{ return m_profile; }


	virtual bool is_ready(bool passive = false) = 0;

	virtual bool is_active()
	{ return is_ready(false); }

	bool is_active(const std::shared_ptr<io>& io)
	{
		m_io = io;
		if (!is_active()) {
			m_io.reset();
			return false;
		}

		return true;
	}

	virtual void writeln(const std::string& str = "")
	{ m_io->writeln(str); }

	virtual void write(const std::string& str)
	{ m_io->write(str); }

	virtual bool foreach_line(std::function<bool(const std::string&)> f, unsigned timeout = 0, unsigned timeout_line = 0) const;

	virtual std::string readln(unsigned timeout = 0) const
	{ return m_io->readln(timeout ? timeout : this->timeout()); }

	virtual bool pending(unsigned timeout = 0) const
	{ return m_io->pending(timeout ? timeout : this->timeout()); }

	static interface::sp detect(const io::sp& io, const profile::sp& sp = nullptr);
	static interface::sp create(const std::string& specl, const std::string& profile = "");

	virtual bcm2_interface id() const = 0;

	protected:
	virtual uint32_t timeout() const
	{ return 50; }

	std::shared_ptr<io> m_io;
	profile::sp m_profile;
};
}

#endif

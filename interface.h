#ifndef BCM2DUMP_INTERFACE_H
#define BCM2DUMP_INTERFACE_H
#include <memory>
#include <string>
#include <vector>
#include "io.h"

namespace bcm2dump {

class interface
{
	public:
	typedef std::shared_ptr<interface> sp;

	virtual ~interface() {}

	virtual std::string name() const = 0;
	virtual void runcmd(const std::string& cmd) = 0;
	virtual bool is_active() = 0;

	bool is_active(const std::shared_ptr<io>& io)
	{
		m_io = io;
		if (!is_active()) {
			m_io.reset();
			return false;
		}

		return true;
	}

	void writeln(const std::string& str = "")
	{ m_io->writeln(str); }

	void write(const std::string& str)
	{ m_io->write(str); }

	std::string readln(unsigned timeout = 100) const
	{ return m_io->readln(timeout); }

	bool pending(unsigned timeout = 100) const
	{ return m_io->pending(); }

	static std::shared_ptr<interface> detect(const std::shared_ptr<io>& io);

	protected:
	std::shared_ptr<io> m_io;
};
}

#endif

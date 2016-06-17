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
	friend class reader_writer;
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
		if (!is_ready()) {
			m_io.reset();
			return false;
		}

		return true;
	}

	virtual void writeln(const std::string& str = "")
	{ m_io->writeln(str); }

	virtual void write(const std::string& str)
	{ m_io->write(str); }

	virtual std::string readln(unsigned timeout = 100) const
	{ return m_io->readln(timeout); }

	virtual bool pending(unsigned timeout = 100) const
	{ return m_io->pending(); }

	static std::shared_ptr<interface> detect(const std::shared_ptr<io>& io);
	static interface::sp create(const std::string& spec);

	virtual bcm2_interface id() const = 0;

	protected:
	std::shared_ptr<io> m_io;
	profile::sp m_profile;
};

class reader_writer
{
	public:
	typedef std::function<void(uint32_t)> progress_listener;
	typedef std::map<std::string, std::string> args;

	virtual ~reader_writer()
	{ do_cleanup(); }

	virtual void set_progress_listener(const progress_listener& listener = progress_listener())
	{ m_listener = listener; }

	virtual void set_partition(const addrspace::part& partition)
	{ m_partition = &partition; }

	virtual void set_interface(const interface::sp& intf)
	{ m_intf = intf; }

	virtual void set_args(const args& args)
	{ m_args = args; }

	struct interrupted {};

	protected:
	static void throw_if_interrupted()
	{
		if (is_interrupted()) {
			throw interrupted();
		}
	}

	static bool is_interrupted()
	{ return s_sigint; }

	virtual void init(uint32_t offset, uint32_t length) {}
	virtual void cleanup() {}

	virtual void update_progress(uint32_t offset)
	{
		if (m_listener) {
			m_listener(offset);
		}
	}

	void do_cleanup();
	void do_init(uint32_t offset, uint32_t length);

	template<class T> T arg(const std::string& name)
	{
		return lexical_cast<T>(m_args[name]);
	}

	std::string arg(const std::string& name)
	{
		return m_args[name];
	}

	progress_listener m_listener;
	interface::sp m_intf;
	const addrspace::part* m_partition;

	const bcm2_addrspace* m_space = nullptr;
	args m_args;

	private:
	static void handle_signal(int signal)
	{ s_sigint = 1; }

	bool m_inited = false;
	static volatile sig_atomic_t s_sigint;
	static unsigned s_count;
};

}

#endif

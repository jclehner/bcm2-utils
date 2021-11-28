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

#undef interface

namespace bcm2dump {

class interface : public std::enable_shared_from_this<interface>
{
	typedef bcm2dump::profile profile_type;
	typedef bcm2dump::version version_type;

	public:
	typedef std::shared_ptr<interface> sp;

	virtual ~interface() {}

	virtual std::string name() const = 0;

	virtual void set_profile(const profile::sp& profile, const version& version)
	{
		set_profile(profile);
		m_version = version;
	}

	virtual void set_profile(const profile::sp& profile)
	{ m_profile = profile; }

	virtual profile_type::sp profile() const
	{ return m_profile; }

	virtual const version_type& version() const
	{ return m_version; }

	virtual bool has_version() const
	{ return !m_version.name().empty(); }

	virtual bool is_privileged() const
	{ return true; }

	virtual void elevate_privileges() {}

	static interface::sp detect(const io::sp& io, const profile::sp& sp = nullptr);
	static interface::sp create(const std::string& specl, const std::string& profile = "");

	virtual bcm2_interface id() const = 0;

	protected:
	void initialize(const profile::sp& profile);

	virtual void initialize_impl()
	{}

	virtual void detect_profile()
	{}

	virtual void detect_version()
	{}

	protected:
	profile::sp m_profile;
	version_type m_version;
};

class cmdline_interface : public interface
{
	public:
	std::vector<std::string> run(const std::string& cmd, unsigned timeout = 0);
	bool run(const std::string& cmd, const std::string& expect, bool stop_on_match = false);

	virtual bool is_ready(bool passive = false) = 0;
	virtual bool wait_ready(unsigned timeout = 5000);
	virtual bool wait_quiet(unsigned timeout = 500) const;

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

	bool foreach_line_raw(std::function<bool(const std::string&)> f, unsigned timeout = 0, bool restart = false) const;
	bool foreach_line(std::function<bool(const std::string&)> f, unsigned timeout = 0) const;

	virtual std::string readln(unsigned timeout = 0) const;

	virtual bool pending(unsigned timeout = 0) const
	{ return m_io->pending(timeout ? timeout : this->timeout()); }

	protected:
	virtual void call(const std::string& cmd)
	{ writeln(cmd); }

	virtual bool is_crash_line(const std::string& line) const
	{ return false; }

	virtual bool check_for_prompt(const std::string& line) const = 0;

	virtual uint32_t timeout() const
	{ return 50; }

	std::shared_ptr<io> m_io;
};

}

#endif

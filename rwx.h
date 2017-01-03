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

#ifndef BCM2DUMP_DUMPER_H
#define BCM2DUMP_DUMPER_H
#include <memory>
#include <string>
#include "interface.h"
#include "profile.h"
#include "ps.h"

namespace bcm2dump {
class rwx //: public rwx_writer
{
	public:
	static unsigned constexpr cap_read = (1 << 0);
	static unsigned constexpr cap_write = (1 << 1);
	static unsigned constexpr cap_exec = (1 << 2);
	static unsigned constexpr cap_special = (1 << 3);
	static unsigned constexpr cap_rw = cap_read | cap_write;
	static unsigned constexpr cap_rwx = cap_rw | cap_exec;

	typedef std::function<void(uint32_t, uint32_t, bool, bool)> progress_listener;
	typedef std::function<void(uint32_t, const ps_header&)> image_listener;
	typedef std::shared_ptr<rwx> sp;
	struct interrupted : public std::exception {};

	struct limits
	{
		public:
		limits(uint32_t alignment = 0, uint32_t min = 0, uint32_t max = 0)
		: alignment(alignment), min(min), max(max) {}

		const uint32_t alignment;
		const uint32_t min;
		const uint32_t max;
	};

	rwx();
	virtual ~rwx();

	virtual limits limits_read() const = 0;
	virtual limits limits_write() const = 0;

	virtual unsigned capabilities() const
	{ return cap_read; }

	void dump(const std::string& spec, std::ostream& os, bool resume = false);
	void dump(uint32_t offset, uint32_t length, std::ostream& os, bool resume = false);
	std::string read(uint32_t offset, uint32_t length);


	void write(const std::string& spec, std::istream& is);
	void write(uint32_t offset, std::istream& is, uint32_t length = 0);
	void write(uint32_t offset, const std::string& buf, uint32_t length = 0);

	void exec(uint32_t offset);

	//bool imgscan(uint32_t offset, uint32_t length, uint32_t steps, ps_header& hdr);

	static sp create(const interface::sp& interface, const std::string& type, bool safe = true);
	static sp create_special(const interface::sp& intf, const std::string& type);

	virtual void set_progress_listener(const progress_listener& l = progress_listener())
	{ m_prog_l = l; }

	virtual void set_image_listener(const image_listener& l = image_listener())
	{ m_img_l = l; }

	virtual void set_partition(const addrspace::part& partition)
	{ m_partition = partition; }

	virtual void set_interface(const interface::sp& intf)
	{ m_intf = intf; }

	virtual void set_addrspace(const addrspace& space)
	{ m_space = space; }

	virtual const addrspace& space() const
	{ return m_space; }

	virtual void silent(bool silent) final
	{ m_silent = silent; }

	static bool was_interrupted()
	{ return s_sigint; }

	protected:
	void require_capability(unsigned cap);

	virtual void init(uint32_t offset, uint32_t length, bool write) {}
	virtual void cleanup() {}

	bool is_inited() const
	{ return m_inited; }

	void do_init(uint32_t offset, uint32_t length, bool write)
	{
		if (!m_inited) {
			init(offset, length, write);
			m_inited = true;
		}
	}

	void do_cleanup()
	{
		if (m_inited) {
			cleanup();
			m_inited = false;
		}
	}

	void read_special(uint32_t offset, uint32_t length, std::ostream& os);
	virtual std::string read_special(uint32_t offset, uint32_t length) = 0;

	virtual std::string read_chunk(uint32_t offset, uint32_t length) = 0;
	// chunk length is guaranteed to be either min_length_write() or max_length_write()
	virtual bool write_chunk(uint32_t offset, const std::string& chunk)
	{ return false; }

	virtual bool exec_impl(uint32_t offset)
	{ return false; }

	static void throw_if_interrupted()
	{
		if (was_interrupted()) {
			throw interrupted();
		}
	}

	virtual void update_progress(uint32_t offset, uint32_t length, bool write = false, bool init = false)
	{
		if (m_prog_l && !m_silent) {
			m_prog_l(offset, length, write, init);
		}
	}

	virtual void init_progress(uint32_t offset, uint32_t length, bool write)
	{
		update_progress(offset, length, write, true);
	}

	virtual void end_progress(bool write)
	{
		update_progress(UINT32_MAX, UINT32_MAX, write, false);
	}

	virtual void image_detected(uint32_t offset, const ps_header& hdr)
	{
		if (m_img_l && !m_silent) {
			m_img_l(offset, hdr);
		}
	}

	interface::sp m_intf;
	progress_listener m_prog_l;
	image_listener m_img_l;
	addrspace::part m_partition;
	addrspace m_space;


	class scoped_cleaner
	{
		public:
		scoped_cleaner(rwx *rwx) : m_rwx(rwx) {}
		~scoped_cleaner()
		{
			if (m_rwx) {
				m_rwx->do_cleanup();
			}
		}

		private:
		rwx *m_rwx;
	};

	scoped_cleaner make_cleaner()
	{ return scoped_cleaner(this); }

	private:
	static void handle_sigint(int signal)
	{ s_sigint = 1; }

	bool m_inited = false;
	bool m_silent = false;

	static unsigned s_count;
	static sigh_type s_sighandler_orig;
	static volatile sig_atomic_t s_sigint;
};


}
#endif

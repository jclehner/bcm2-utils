#ifndef BCM2DUMP_DUMPER_H
#define BCM2DUMP_DUMPER_H
#include <memory>
#include <string>
#include "interface.h"
#include "profile.h"
#include "ps.h"

namespace bcm2dump {
class reader //: public reader_writer
{
	public:
	static unsigned constexpr cap_read = (1 << 0);
	static unsigned constexpr cap_write = (1 << 1);
	static unsigned constexpr cap_exec = (1 << 2);

	typedef std::function<void(uint32_t, uint32_t, bool)> progress_listener;
	typedef std::function<void(uint32_t, const ps_header&)> image_listener;
	typedef std::shared_ptr<reader> sp;
	struct interrupted : public std::exception {};

	struct limits
	{
		public:
		limits(uint32_t alignment, uint32_t min, uint32_t max)
		: alignment(alignment), min(min), max(max) {}

		const uint32_t alignment;
		const uint32_t min;
		const uint32_t max;
	};

	virtual ~reader() {}

	virtual limits limits_read() const = 0;
	virtual limits limits_write() const = 0;

	virtual unsigned capabilities() const
	{ return cap_read; }

	void dump(const addrspace::part& partition, std::ostream& os, uint32_t length = 0);
	void dump(uint32_t offset, uint32_t length, std::ostream& os);
	std::string read(uint32_t offset, uint32_t length);

	void write(uint32_t offset, std::istream& is, uint32_t length = 0);
	void write(uint32_t offset, const std::string& buf, uint32_t length = 0);

	void exec(uint32_t offset);

	//bool imgscan(uint32_t offset, uint32_t length, uint32_t steps, ps_header& hdr);

	static sp create(const interface::sp& interface, const std::string& type, bool no_dumpcode = false);

	virtual void set_progress_listener(const progress_listener& l = progress_listener())
	{ m_prog_l = l; }

	virtual void set_image_listener(const image_listener& l = image_listener())
	{ m_img_l = l; }

	virtual void set_partition(const addrspace::part& partition)
	{ m_partition = &partition; }

	virtual void set_interface(const interface::sp& intf)
	{ m_intf = intf; }

	virtual void set_addrspace(const addrspace& space)
	{ m_space = space; }

	static bool was_interrupted()
	{ return s_sigint; }

	protected:
	void require_capability(unsigned cap);

	virtual void init(uint32_t offset, uint32_t length, bool write) {}
	virtual void cleanup() {}

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

	virtual void update_progress(uint32_t offset, uint32_t length, bool init = false)
	{
		if (m_prog_l) {
			m_prog_l(offset, length, init);
		}
	}

	virtual void image_detected(uint32_t offset, const ps_header& hdr)
	{
		if (m_img_l) {
			m_img_l(offset, hdr);
		}
	}

	interface::sp m_intf;
	progress_listener m_prog_l;
	image_listener m_img_l;
	const addrspace::part* m_partition = nullptr;
	addrspace m_space;


	class scoped_cleaner
	{
		public:
		scoped_cleaner(reader *reader) : m_reader(reader) {}
		~scoped_cleaner()
		{
			if (m_reader) {
				m_reader->do_cleanup();
			}
		}

		private:
		reader *m_reader;
	};

	scoped_cleaner make_cleaner()
	{ return scoped_cleaner(this); }

	private:
	static void handle_sigint(int signal)
	{ s_sigint = 1; }

	bool m_inited = false;

	static volatile sig_atomic_t s_sigint;
};


}
#endif

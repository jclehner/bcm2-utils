#ifndef BCM2DUMP_WRITER_H
#define BCM2DUMP_WRITER_H
#include <stdexcept>
#include <iostream>
#include <memory>
#include <string>
#include "interface.h"

namespace bcm2dump {
class writer : public interface_rw_base
{
	public:
	typedef std::shared_ptr<writer> sp;

	virtual void set_partition(const std::string& partition)
	{ m_partition = partition; }

	virtual void set_interface(const interface::sp& intf)
	{ m_intf = intf; }

	virtual uint32_t min_size() const
	{ return 4; }

	virtual uint32_t max_size() const
	{ return 4; }

	void write(uint32_t offset, std::istream& is, uint32_t length = 0);
	void write(uint32_t offset, const std::string& str);

	void exec(uint32_t offset);

	static sp create(const interface::sp& intf, const std::string& type);

	protected:
	virtual bool write_chunk(uint32_t offset, const std::string& chunk) = 0;
	virtual void exec_impl(uint32_t offset)
	{ throw std::runtime_error("not supported"); }
};
}
#endif

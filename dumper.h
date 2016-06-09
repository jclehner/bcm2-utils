#ifndef BCM2DUMP_DUMPER_H
#define BCM2DUMP_DUMPER_H
#include <memory>
#include <string>
#include "interface.h"
#include "profile.h"

namespace bcm2dump {
class dumper
{
	public:
	typedef std::shared_ptr<dumper> sp;

	struct params
	{
		uint32_t offset;
		uint32_t length;
		bcm2_partition* partition;
		bcm2_addrspace* space;
		const char* filename;
	};

	virtual ~dumper() {}

	virtual void set_partition(const std::string& partition)
	{ m_partition = partition; }

	virtual uint32_t offset_alignment() const
	{ return 4; }

	virtual uint32_t length_alignment() const
	{ return 16; }

	virtual uint32_t chunk_size() const = 0;

	virtual std::string read_chunk(uint32_t offset, uint32_t length) = 0;

	static sp get(const interface::sp& interface, const bcm2_profile* profile, const bcm2_addrspace* space);

#if 1
	// XXX for testing only
	static sp get_bfc_ram(const interface::sp& interface);
	static sp get_bfc_flash(const interface::sp& interface);
	static sp get_bootloader_ram(const interface::sp& interface);
#endif

	protected:
	interface::sp m_intf;
	std::string m_partition;
};
}
#endif

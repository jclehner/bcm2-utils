#ifndef BCM2DUMP_DUMPER_H
#define BCM2DUMP_DUMPER_H
#include <memory>
#include <string>
#include "interface.h"
#include "profile.h"

namespace bcm2dump {
class dumper : public interface_rw_base
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

	virtual ~dumper() { do_cleanup(); }

	virtual uint32_t offset_alignment() const
	{ return 4; }

	virtual uint32_t length_alignment() const
	{ return 16; }

	virtual uint32_t chunk_size() const = 0;

	void dump(uint32_t offset, uint32_t length, std::ostream& os);
	std::string dump(uint32_t offset, uint32_t length);

	static sp create(const interface::sp& interface, const std::string& type);

	protected:
	virtual std::string read_chunk(uint32_t offset, uint32_t length) = 0;
};
}
#endif

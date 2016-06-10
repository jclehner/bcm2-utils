#include <arpa/inet.h>
#include <sstream>
#include "writer.h"
#include "util.h"
using namespace std;

namespace bcm2dump {
namespace {

class bootloader_ram_writer : public writer
{
	protected:
	virtual bool write_chunk(uint32_t offset, const string& chunk) override
	{
		try {
			// just to be safe
			m_intf->writeln("\r\n");

			if (!m_intf->runcmd("w", "Write memory.", true)) {
				return false;
			}

			m_intf->writeln(to_hex(offset));
			uint32_t val = ntohl(extract<uint32_t>(chunk));
			return m_intf->runcmd(to_hex(val) + "\r\n", "Main Menu");
		} catch (const exception& e) {
			// ensure that we're in a sane state
			m_intf->runcmd("\r\n", "Main Menu");
			// TODO log
			return false;
		}
	}

	virtual void exec_impl(uint32_t offset) override
	{
		m_intf->runcmd("j");
		m_intf->writeln(to_hex(offset));
	}

};

class bfc_ram_writer : public writer
{
	public:
	virtual uint32_t min_size() const override
	{ return 1; }

	protected:
	virtual bool write_chunk(uint32_t offset, const string& chunk) override
	{
		uint32_t val = chunk.size() == 4 ? ntohl(extract<uint32_t>(chunk)) : chunk[0];
		return m_intf->runcmd("/write_memory -s " + to_string(chunk.size()) + " 0x" + 
				to_hex(offset) + " 0x" + to_hex(val), "Writing");
	}

	virtual void exec_impl(uint32_t offset) override
	{
		m_intf->runcmd("/call func -a 0x" + to_hex(offset));
	}
};

class bfc_flash_writer : public writer
{
	public:
	virtual uint32_t min_size() const override
	{ return 1; }

	protected:
	virtual void init(uint32_t offset, uint32_t length) override
	{
		if (!m_intf->runcmd("/flash/open " + arg("partition"), "opened")) {
			throw runtime_error("failed to open partition: " + arg("partition"));
		}
	}

	virtual void cleanup() override
	{
		if (!m_intf->runcmd("/flash/close", "closed")) {
			// TODO log
		}
	}

	virtual bool write_chunk(uint32_t offset, const string& chunk) override
	{
		uint32_t val = chunk.size() == 4 ? ntohl(extract<uint32_t>(chunk)) : chunk[0];
		return m_intf->runcmd("/flash/write " + to_string(chunk.size()) + " 0x" 
				+ to_hex(offset) + " 0x" + to_hex(val), "value written");
	}
};

template<class T> writer::sp create_writer(const interface::sp& intf)
{
	writer::sp writer = make_shared<T>();
	writer->set_interface(intf);
	return writer;
}
}

void writer::write(uint32_t offset, std::istream& is, uint32_t length)
{
	if (!length) {
		streampos cur = is.tellg();
		is.seekg(0, ios_base::end);
		length = is.tellg() - cur;
		if (!length) {
			throw runtime_error("failed to determine length of stream");
		}
	}

	string buf;
	buf.reserve(length);
	if (is.readsome(&buf[0], length) != length) {
		throw runtime_error("failed to read " + to_string(length) + " bytes");
	}

	write(offset, buf);
}

void writer::write(uint32_t offset, const string& buf)
{
	uint32_t length = buf.size();
	if (length % min_size()) {
		throw invalid_argument("length must be aligned to " + to_string(min_size()) + " bytes");
	}

	// 2 byte values can only be written at a 2 byte aligned offset, and so forth
	uint32_t alignment = max(length, min_size());
	if (offset % alignment) {
		throw invalid_argument("offset must be aligned to " + to_string(alignment) + " bytes");
	}

	uint32_t remaining = length;

	while (remaining) {
		uint32_t n = length < max_size() ? min_size() : max_size();
		if (!write_chunk(offset, buf.substr(buf.size() - remaining, n))) {
			throw runtime_error("failed to write chunk (@" + to_hex(offset) + ", " + to_string(n) + ")");
		}

		update_progress(offset, n);

		remaining -= n;
		offset += n;
	}
}

void writer::exec(uint32_t offset)
{
	if (offset % 4) {
		throw invalid_argument("offset must be aligned to 4 bytes");
	}

	exec_impl(offset);
}

writer::sp writer::create(const interface::sp& intf, const string& type)
{
	if (intf->name() == "bootloader") {
		if (type == "ram") {
			return create_writer<bootloader_ram_writer>(intf);
		}
	} else if (intf->name() == "bfc") {
		if (type == "ram") {
			return create_writer<bfc_ram_writer>(intf);
		} else if (type == "flash") {
			return create_writer<bfc_flash_writer>(intf);
		}
	}

	throw invalid_argument("no such writer: " + intf->name() + "-" + type);
}
}

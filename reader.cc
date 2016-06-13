#include <arpa/inet.h>
#include <iostream>
#include <fstream>
#include "bcm2dump.h"
#include "mipsasm.h"
#include "writer.h"
#include "reader.h"
#include "util.h"

using namespace std;

namespace bcm2dump {
namespace {

template<class T> T hex_cast(const std::string& str)
{
	return lexical_cast<T>(str, 16);
}

inline void patch32(string& buf, string::size_type offset, uint32_t n)
{
	patch<uint32_t>(buf, offset, htonl(n));
}

template<class T> string to_buf(const T& t)
{
	return string(reinterpret_cast<const char*>(&t), sizeof(T));
}

template<class T> T align_to(const T& num, const T& alignment)
{
	if (num % alignment) {
		return ((num / alignment) + 1) * alignment;
	}

	return num;
}

uint32_t calc_checksum(const string& buf)
{
	uint32_t checksum = 0xdeadbeef;

	for (char c : buf) {
		checksum ^= (c * 0xffffff);
	}

	return checksum;
}

// reader implementation an interactive text-based
// user interface
class parsing_reader : public reader
{
	public:
	virtual string read_chunk(uint32_t offset, uint32_t length) override final
	{
		return read_chunk_impl(offset, length, 0);
	}

	virtual uint32_t chunk_size() const override
	{ return 2048; }

	protected:
	virtual string read_chunk_impl(uint32_t offset, uint32_t length, uint32_t retries);
	// issues a command that displays the requested chunk
	virtual void do_read_chunk(uint32_t offset, uint32_t length) = 0;
	// checks if the line is junk (as opposed to a possible chunk line)
	virtual bool is_ignorable_line(const string& line) = 0;
	// parses one line of data
	virtual string parse_chunk_line(const string& line, uint32_t offset) = 0;
	// called if a chunk was not successfully read
	virtual void on_chunk_retry(uint32_t offset, uint32_t length) {}
};

string parsing_reader::read_chunk_impl(uint32_t offset, uint32_t length, uint32_t retries)
{
	do_read_chunk(offset, length);

	string line, linebuf, chunk, last;
	uint32_t pos = offset;

	while (chunk.size() < length && m_intf->pending()) {
		line = trim(m_intf->readln());

		if (is_ignorable_line(line)) {
			continue;
		} else {
			try {
				string linebuf = parse_chunk_line(line, pos);
				pos += linebuf.size();
				chunk += linebuf;
				last = line;
				update_progress(pos);
			} catch (const exception& e) {
				if (retries >= 2) {
					throw runtime_error("failed to read chunk line @" + to_hex(pos) + ": '" + line + "' (" + e.what() + ")");
				}

				// TODO log
				break;
			}
		}
	}

	if (chunk.size() != length) {
		if (retries >= 2) {
			throw runtime_error("read incomplete chunk 0x" + to_hex(offset)
					+ ": " + to_string(chunk.size()) + "/" +to_string(length)
					+ " b; last line:\n'" + last + "'");
		}
			
		on_chunk_retry(offset, length);

		logger::d() << "retrying chunk 0x" << to_hex(offset) << endl;
		return read_chunk_impl(offset, length, retries + 1);
	}

	return chunk;
}

class bfc_ram_reader : public parsing_reader
{
	public:
	virtual uint32_t length_alignment() const override
	{ return 16; }

	virtual uint32_t chunk_size() const override
	{ return 0x4000; }

	virtual void do_read_chunk(uint32_t offset, uint32_t length) override;
	virtual bool is_ignorable_line(const string& line) override;
	virtual string parse_chunk_line(const string& line, uint32_t offset) override;
};

void bfc_ram_reader::do_read_chunk(uint32_t offset, uint32_t length)
{
	m_intf->runcmd("/read_memory -s 4 -n " + to_string(length) + " 0x" + to_hex(offset));
}

bool bfc_ram_reader::is_ignorable_line(const string& line)
{
	if (line.size() >= 65) {
		if (line[8] == ':' || line.substr(48, 3) == " | ") {
			return false;
		}
	}

	return true;
}

string bfc_ram_reader::parse_chunk_line(const string& line, uint32_t offset)
{
	if (offset != hex_cast<uint32_t>(line.substr(0, 8))) {
		throw runtime_error("offset mismatch");
	}

	string linebuf;
	for (unsigned i = 0; i < 4; ++i) {
		linebuf += to_buf(htonl(hex_cast<uint32_t>(line.substr((i + 1) * 10, 8))));
	}

	return linebuf;
}

class bfc_flash_reader : public parsing_reader
{
	public:
	virtual uint32_t chunk_size() const override
	{ return 8192; }

	protected:
	virtual void init(uint32_t offset, uint32_t length) override;
	virtual void cleanup() override;

	virtual void do_read_chunk(uint32_t offset, uint32_t length) override;
	virtual bool is_ignorable_line(const string& line) override;
	virtual string parse_chunk_line(const string& line, uint32_t offset) override;
};

void bfc_flash_reader::init(uint32_t offset, uint32_t length)
{
	if (arg("partition").empty()) {
		throw runtime_error("cannot dump without a partition name");
	}

	cleanup();
	if (!m_intf->runcmd("/flash/open " + arg("partition"), "driver opened")) {
		throw runtime_error("failed to open partition " + arg("partition"));
	}
}

void bfc_flash_reader::cleanup()
{
	m_intf->runcmd("/flash/close", "driver closed");
}

void bfc_flash_reader::do_read_chunk(uint32_t offset, uint32_t length)
{
	m_intf->runcmd("/flash/readDirect " + to_string(length) + " " + to_string(offset));
}

bool bfc_flash_reader::is_ignorable_line(const string& line)
{
	if (line.size() >= 53) {
		if (line.substr(11, 3) == "   " && line.substr(25, 3) == "   ") {
			return false;
		}
	}	

	return true;
}

string bfc_flash_reader::parse_chunk_line(const string& line, uint32_t offset)
{
	string linebuf;

	for (unsigned i = 0; i < 16; ++i) {
		// don't change this to uint8_t
		uint32_t val = hex_cast<uint32_t>(line.substr(i * 3 + (i / 4) * 2, 2));
		if (val > 0xff) {
			throw runtime_error("value out of range: 0x" + to_hex(val));
		}

		linebuf += char(val);
	}

	return linebuf;
}

class bootloader_ram_reader : public parsing_reader
{
	public:
	virtual uint32_t chunk_size() const override
	{ return 4; }

	virtual uint32_t length_alignment() const
	{ return 4; }


	protected:
	virtual void init(uint32_t offset, uint32_t length) override;
	virtual void cleanup() override;

	virtual void do_read_chunk(uint32_t offset, uint32_t length) override;
	virtual bool is_ignorable_line(const string& line) override;
	virtual string parse_chunk_line(const string& line, uint32_t offset) override;
};

void bootloader_ram_reader::init(uint32_t offset, uint32_t length)
{
	m_intf->write("r");
}

void bootloader_ram_reader::cleanup()
{
	m_intf->writeln();
	m_intf->writeln();
}

void bootloader_ram_reader::do_read_chunk(uint32_t offset, uint32_t length)
{
	m_intf->writeln("0x" + to_hex(offset));
}

bool bootloader_ram_reader::is_ignorable_line(const string& line)
{
	if (contains(line, "Value at") || contains(line, "(hex)")) {
		return false;
	}

	return true;
}

string bootloader_ram_reader::parse_chunk_line(const string& line, uint32_t offset)
{
	if (line.find("Value at") == 0) {
		if (offset != hex_cast<uint32_t>(line.substr(9, 8))) {
			throw runtime_error("offset mismatch");
		}

		return to_buf(htonl(hex_cast<uint32_t>(line.substr(19, 8))));
	}

	throw runtime_error("unexpected line");
}

// this defines uint32 dumpcode[]
#include "dumpcode.h"

class dumpcode_reader : public parsing_reader
{
	public:
	dumpcode_reader(const bcm2_func* func = nullptr) : m_dump_func(func) {}

	virtual uint32_t chunk_size() const
	{ return 0x4000; }

	virtual void set_interface(const interface::sp& intf) override
	{
		parsing_reader::set_interface(intf);

		if (!intf->profile()) {
			throw runtime_error("dumpcode requires a profile");
		}

		const codecfg& cfg = intf->profile()->codecfg(intf->id());

		if (!cfg.loadaddr || !cfg.buffer || !cfg.printf) {
			throw runtime_error("insufficient profile infos for dumpcode");
		} else if (cfg.loadaddr & 0xffff) {
			throw runtime_error("loadaddr must be aligned to 64k");
		}

		m_ramw = writer::create(intf, "ram");
		m_ramr = reader::create(intf, "ram");
	}

	protected:
	virtual void do_read_chunk(uint32_t offset, uint32_t length) override
	{
		m_ramw->exec(m_loadaddr + m_entry);
	}

	virtual bool is_ignorable_line(const string& line) override
	{
		if (line.size() >= 8 && line.size() <= 36) {
			if (line[0] == ':') {
				return false;
			}
		}

		return true;
	}

	virtual string parse_chunk_line(const string& line, uint32_t offset) override
	{
		string linebuf;
		string::size_type beg = 1;

		for (unsigned i = 0; i < 4; ++i) {
			string valstr = line.substr(beg, line.find(':', beg + 1) - beg);
			linebuf += to_buf(htonl(hex_cast<uint32_t>(valstr)));
			beg += valstr.size() + 1;
		}

		return linebuf;
	}

	private:
	void on_chunk_retry(uint32_t offset, uint32_t length)
	{
		if (!arg("codefile").empty()) {
			throw runtime_error("error recovery is not possible with custom dumpcode");
		}

		patch32(m_code, 0x10, offset);
		m_ramw->write(m_loadaddr + 0x10, m_code.substr(0x10, 4));

		patch32(m_code, 0x1c, m_dump_length - (offset - m_dump_offset));
		m_ramw->write(m_loadaddr + 0x1c, m_code.substr(0x1c, 4));
	}

	void init_dump_func()
	{
		for (size_t i = 0; i < BCM2_INTF_NUM; ++i) {
			if (m_space->read[i].addr && (m_space->read[i].intf & m_intf->id())) {
				m_dump_func = &m_space->read[i];
				return;
			}
		}

		if (!m_space->mem) {
			throw runtime_error("no 'read' function defined for " + string(m_space->name));
		}
	}

	void init(uint32_t offset, uint32_t length) override
	{
		const profile::sp& profile = m_intf->profile();
		const codecfg& cfg = profile->codecfg(m_intf->id());

		if (cfg.buflen && length > cfg.buflen) {
			throw runtime_error("requested length exceeds buffer size ("
					+ to_string(cfg.buflen) + " b)");
		}

		m_dump_offset = offset;
		m_dump_length = length;

		uint32_t kseg1 = profile->kseg1();
		m_loadaddr = kseg1 | cfg.loadaddr;

		if (arg("codefile").empty()) {
			m_code = string(reinterpret_cast<const char*>(dumpcode), sizeof(dumpcode));
			m_entry = 0x4c;

			patch32(m_code, 0x10, 0);
			patch32(m_code, 0x14, kseg1 | cfg.buffer);
			patch32(m_code, 0x18, offset);
			patch32(m_code, 0x1c, length);
			patch32(m_code, 0x20, chunk_size());
			patch32(m_code, 0x24, kseg1 | cfg.printf);

			if (m_dump_func && m_dump_func->addr) {
				patch32(m_code, 0x0c, m_dump_func->mode);
				patch32(m_code, 0x28, kseg1 | m_dump_func->addr);

				for (unsigned i = 0; i < BCM2_PATCH_NUM; ++i) {
					uint32_t offset = 0x2c + (8 * i);
					uint32_t addr = m_dump_func->patch[i].addr;
					patch32(m_code, offset, addr ? (kseg1 | addr) : 0);
					patch32(m_code, offset + 4, addr ? m_dump_func->patch[i].word : 0);
				}
			}

			uint32_t codesize = m_code.size();
			if (mipsasm_resolve_labels(reinterpret_cast<uint32_t*>(&m_code[0]), &codesize, m_entry) != 0) {
				throw runtime_error("failed to resolve mips asm labels");
			}

			m_code.resize(codesize);
			uint32_t expected = calc_checksum(m_code.substr(m_entry, m_code.size() - 4 - m_entry));
			uint32_t actual = ntohl(extract<uint32_t>(m_ramr->read(m_loadaddr + m_code.size() - 4, 4)));
			bool quick = (expected == actual);

			patch32(m_code, codesize - 4, expected);

			progress pg;
			progress_init(&pg, m_loadaddr, m_code.size());

			if (m_listener && !quick) {
				printf("updating dump code at 0x%08x (%u b)\n", m_loadaddr, codesize);
			}

			for (unsigned pass = 0; pass < 2; ++pass) {
				string ramcode = m_ramr->read(m_loadaddr, m_code.size());
				for (uint32_t i = 0; i < m_code.size(); i += 4) {
					if (!quick && pass == 0 && m_listener) {
						progress_add(&pg, 4);
						printf("\r ");
						progress_print(&pg, stdout);
					}

					if (ramcode.substr(i, 4) != m_code.substr(i, 4)) {
						if (pass == 1) {
							throw runtime_error("dump code verification failed at 0x" + to_hex(i + m_loadaddr, 8));
						}
						m_ramw->write(m_loadaddr + i, m_code.substr(i, 4));
					}
				}

				if (!quick && pass == 0 && m_listener) {
					printf("\n");
				}
			}
		}
	}

	string m_code;
	uint32_t m_loadaddr = 0;
	uint32_t m_entry = 0;

	uint32_t m_dump_offset = 0;
	uint32_t m_dump_length = 0;

	const bcm2_func* m_dump_func = nullptr;

	writer::sp m_ramw;
	reader::sp m_ramr;
};

template<class T> reader::sp create_reader(const interface::sp& intf)
{
	reader::sp ret = make_shared<T>();
	ret->set_interface(intf);
	return ret;
}
}

void reader::dump(uint32_t offset, uint32_t wbytes, std::ostream& os)
{
	if (offset % offset_alignment()) {
		throw invalid_argument("offset not aligned to " + to_string(offset_alignment()) + " bytes");
	}

	uint32_t rbytes = align_to(wbytes, length_alignment());
	if (wbytes != rbytes) {
		// TODO log
	}

	do_init(offset, rbytes);

	while (rbytes) {
		uint32_t n = min(chunk_size(), rbytes);
		string chunk = read_chunk(offset, n);

		update_progress(offset + n);

		if (chunk.size() != n) {
			throw runtime_error("unexpected chunk length: " + to_string(chunk.size()));
		}

		os.write(chunk.c_str(), min(n, wbytes));

		wbytes = (wbytes >= n) ? wbytes - n : 0;
		rbytes -= n;
		offset += n;
	}

	do_cleanup();
}

void reader::dump(const addrspace::part& partition, ostream& os)
{
	set_partition(partition.altname());
	dump(partition.offset(), partition.size(), os);
}

string reader::read(uint32_t offset, uint32_t length)
{
	ostringstream ostr;
	dump(offset, length, ostr);
	return ostr.str();
}

// TODO this should be migrated to something like
// interface::create_reader(const string& type)
reader::sp reader::create(const interface::sp& intf, const string& type, bool no_dumpcode)
{
	if (intf->name() == "bootloader") {
		if (type == "ram") {
			if (no_dumpcode) {
				return create_reader<bootloader_ram_reader>(intf);
			} else {
				return create_reader<dumpcode_reader>(intf);
			}
		} else if (type == "flash") {
			// TODO
		}
	} else if (intf->name() == "bfc") {
		if (type == "ram") {
			return create_reader<bfc_ram_reader>(intf);
		} else if (type == "flash") {
			return create_reader<bfc_flash_reader>(intf);
		}
	}

	throw invalid_argument("no such reader: " + intf->name() + "-" + type);
}
}


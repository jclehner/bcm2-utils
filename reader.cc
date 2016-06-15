#include <arpa/inet.h>
#include <iostream>
#include <fstream>
#include "bcm2dump.h"
#include "mipsasm.h"
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
	virtual ~parsing_reader() {}

	unsigned capabilities() const
	{ return cap_read; }

	protected:
	virtual string read_chunk(uint32_t offset, uint32_t length) override final
	{
		return read_chunk_impl(offset, length, 0);
	}

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
		throw_if_interrupted();

		line = trim(m_intf->readln());

		if (is_ignorable_line(line)) {
			continue;
		} else {
			try {
				string linebuf = parse_chunk_line(line, pos);
				pos += linebuf.size();
				chunk += linebuf;
				last = line;
				update_progress(pos, chunk.size());
			} catch (const exception& e) {
				string msg = "failed to parse chunk line @" + to_hex(pos) + ": '" + line + "' (" + e.what() + ")";
				if (retries >= 2) {
					throw runtime_error(msg);
				}

				logger::d() << endl << msg << endl;
				break;
			}
		}
	}

	if (chunk.size() != length) {
		string msg = "read incomplete chunk 0x" + to_hex(offset)
					+ ": " + to_string(chunk.size()) + "/" +to_string(length);
		if (retries < 2) {
			// if the dump is still underway, we need to wait for it to finish
			// before issuing the next command. wait for up to 10 seconds.

			for (unsigned i = 0; i < 10; ++i) {
				if (m_intf->is_ready(true)) {
					logger::d() << endl << msg << "; retrying" << endl;
					on_chunk_retry(offset, length);
					return read_chunk_impl(offset, length, retries + 1);
				} else if (i != 9) {
					sleep(1);
				}
			}
		}

		throw runtime_error(msg);

	}

	return chunk;
}

class bfc_ram_reader : public parsing_reader
{
	public:
	virtual ~bfc_ram_reader() {}

	virtual limits limits_read() const override
	{ return limits(4, 16, 8192); }

	virtual limits limits_write() const override
	{ return limits(4, 1, 4); }

	unsigned capabilities() const
	{ return cap_read | cap_write | cap_exec; }

	protected:
	virtual bool exec_impl(uint32_t offset) override;
	virtual bool write_chunk(uint32_t offset, const string& chunk) override;
	virtual void do_read_chunk(uint32_t offset, uint32_t length) override;
	virtual bool is_ignorable_line(const string& line) override;
	virtual string parse_chunk_line(const string& line, uint32_t offset) override;

	private:
	bool m_hint_decimal = false;
	bool m_rooted = true;
};

bool bfc_ram_reader::exec_impl(uint32_t offset)
{
	return m_intf->runcmd("/call func -a 0x" + to_hex(offset), "Calling function 0x");
};

bool bfc_ram_reader::write_chunk(uint32_t offset, const string& chunk)
{
	if (m_rooted) {
		uint32_t val = chunk.size() == 4 ? ntohl(extract<uint32_t>(chunk)) : chunk[0];
		return m_intf->runcmd("/write_memory -s " + to_string(chunk.size()) + " 0x" +
				to_hex(offset) + " 0x" + to_hex(val), "Writing");
	} else {
		// diag writemem only supports writing bytes
		for (char c : chunk) {
			if (!m_intf->runcmd("/system/diag/writemem 0x" + to_hex(offset) + " 0x" + to_hex(int(c)), "Writing")) {
				return false;
			}
		}

		return true;
	}
}

void bfc_ram_reader::do_read_chunk(uint32_t offset, uint32_t length)
{
	if (m_rooted) {
		m_intf->runcmd("/read_memory -s 4 -n " + to_string(length) + " 0x" + to_hex(offset));
	} else {
		m_intf->runcmd("/system/diag readmem -s 4 -n " + to_string(length) + " 0x" + to_hex(offset));
	}

	m_hint_decimal = false;
}

bool bfc_ram_reader::is_ignorable_line(const string& line)
{
	if (line.size() >= 65) {
		if (line.substr(8, 2) == ": " && line.substr(48, 3) == " | ") {
			m_hint_decimal = false;
			return false;
		} else if (contains(line, ": ") && contains(line, " | ")) {
			// if another message is printed by the firmware, the dump
			// output sometimes switches to an all-decimal format.
			m_hint_decimal = true;
			return false;
		}
	}

	return true;
}

string bfc_ram_reader::parse_chunk_line(const string& line, uint32_t offset)
{
	string linebuf;

	if (!m_hint_decimal) {
		if (offset != hex_cast<uint32_t>(line.substr(0, 8))) {
			throw runtime_error("offset mismatch");
		}
		for (unsigned i = 0; i < 4; ++i) {
			linebuf += to_buf(htonl(hex_cast<uint32_t>(line.substr((i + 1) * 10, 8))));
		}
	} else {
		auto beg = line.find(": ");
		if (offset != lexical_cast<uint32_t>(line.substr(0, beg))) {
			throw runtime_error("offset mismatch");
		}

		for (unsigned i = 0; i < 4; ++i) {
			beg = line.find_first_of("0123456789", beg);
			auto end = line.find_first_not_of("0123456789", beg);
			linebuf += to_buf(htonl(lexical_cast<uint32_t>(line.substr(beg, end - beg))));
			beg = end;
		}
	}

	return linebuf;
}

class bfc_flash_reader : public parsing_reader
{
	public:
	virtual ~bfc_flash_reader() {}

	virtual limits limits_read() const override
	{ return limits(1, 16, 8192); }

	virtual limits limits_write() const override
	{ return limits(1, 1, 4); }

	protected:
	virtual void init(uint32_t offset, uint32_t length, bool write) override;
	virtual void cleanup() override;

	virtual bool write_chunk(uint32_t offset, const string& buf) override;

	virtual void do_read_chunk(uint32_t offset, uint32_t length) override;
	virtual bool is_ignorable_line(const string& line) override;
	virtual string parse_chunk_line(const string& line, uint32_t offset) override;

	virtual void update_progress(uint32_t offset, uint32_t length, bool init) override
	{
		parsing_reader::update_progress(m_partition->offset() + offset, length, true);
	}
};

void bfc_flash_reader::init(uint32_t offset, uint32_t length, bool write)
{
	if (!m_partition) {
		throw runtime_error("no partition name specified");
	}

	for (unsigned pass = 0; pass < 2; ++pass) {
		m_intf->runcmd("/flash/open " + m_partition->altname());

		bool opened = false;
		bool retry = false;

		while (m_intf->pending()) {
			string line = m_intf->readln();
			if (contains(line, "opened twice")) {
				retry = true;
				opened = false;
			} else if (contains(line, "driver opened") || contains(line, "NandFlashRead")) {
				opened = true;
			}
		}

		if (opened) {
			break;
		} else if (retry && pass == 0) {
			logger::d() << "closing flash driver before reopening" << endl;
			cleanup();
		} else {
			throw runtime_error("failed to open partition " + m_partition->name());
		}
	}
}

void bfc_flash_reader::cleanup()
{
	m_intf->runcmd("/flash/close", "driver closed");
}

bool bfc_flash_reader::write_chunk(uint32_t offset, const std::string& chunk)
{
	uint32_t val = chunk.size() == 4 ? ntohl(extract<uint32_t>(chunk)) : chunk[0];
	return m_intf->runcmd("/flash/write " + to_string(chunk.size()) + " 0x"
			+ to_hex(offset) + " 0x" + to_hex(val), "value written");
}

void bfc_flash_reader::do_read_chunk(uint32_t offset, uint32_t length)
{
	if (offset < m_partition->offset()) {
		// just to be safe. this should never happen
		throw runtime_error("offset 0x" + to_hex(offset) + " is less than partition offset");
	}

	// because readDirect expects an offset *within* the partition
	offset -= m_partition->offset();

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
	virtual ~bootloader_ram_reader() {}

	virtual limits limits_read() const override
	{ return limits(4, 4, 4); }

	virtual limits limits_write() const override
	{ return limits(4, 4, 4); }

	protected:
	virtual void init(uint32_t offset, uint32_t length, bool write) override;
	virtual void cleanup() override;

	virtual bool write_chunk(uint32_t offset, const string& chunk) override;
	virtual bool exec_impl(uint32_t offset) override;

	virtual void do_read_chunk(uint32_t offset, uint32_t length) override;
	virtual bool is_ignorable_line(const string& line) override;
	virtual string parse_chunk_line(const string& line, uint32_t offset) override;
};

void bootloader_ram_reader::init(uint32_t offset, uint32_t length, bool write)
{
	if (write) {
		m_intf->runcmd("");
		m_intf->runcmd("w");
	} else {
		m_intf->runcmd("r");
	}
}

void bootloader_ram_reader::cleanup()
{
	m_intf->writeln();
	m_intf->writeln();
}

bool bootloader_ram_reader::write_chunk(uint32_t offset, const string& chunk)
{
	try {
		if (!m_intf->runcmd("w", "Write memory.", true)) {
			return false;
		}

		m_intf->writeln(to_hex(offset));
		uint32_t val = ntohl(extract<uint32_t>(chunk));
		return m_intf->runcmd(to_hex(val) + "\r\n", "Main Menu");
	} catch (const exception& e) {
		// ensure that we're in a sane state
		m_intf->runcmd("\r\n", "Main Menu");
		return false;
	}
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

bool bootloader_ram_reader::exec_impl(uint32_t offset)
{
	m_intf->runcmd("");
	m_intf->runcmd("j", "");
	m_intf->writeln(to_hex(offset));
	// FIXME
	return true;
}

// this defines uint32 dumpcode[]
#include "dumpcode.h"

class dumpcode_reader : public parsing_reader
{
	public:
	dumpcode_reader(const bcm2_func* func = nullptr) : m_reader_func(func) {}

	virtual limits limits_read() const override
	{ return limits(4, 16, 0x4000); }

	virtual limits limits_write() const override
	{ return limits(0, 0, 0); }

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
		m_ram = reader::create(intf, "ram", true);
	}

	protected:
	virtual void do_read_chunk(uint32_t offset, uint32_t length) override
	{
		m_ram->exec(m_loadaddr + m_entry);
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
	virtual bool write_chunk(uint32_t offset, const string& chunk) override
	{ return false; }

	virtual bool exec_impl(uint32_t offset) override
	{ return false; }

	void on_chunk_retry(uint32_t offset, uint32_t length) override
	{
		if (false) {
			throw runtime_error("error recovery is not possible with custom dumpcode");
		}

		patch32(m_code, 0x10, offset);
		m_ram->write(m_loadaddr + 0x10, m_code.substr(0x10, 4));

		patch32(m_code, 0x1c, m_dump_length - (offset - m_dump_offset));
		m_ram->write(m_loadaddr + 0x1c, m_code.substr(0x1c, 4));
	}

	void init(uint32_t offset, uint32_t length, bool write) override
	{
		const profile::sp& profile = m_intf->profile();
		const codecfg& cfg = profile->codecfg(m_intf->id());

		if (cfg.buflen && length > cfg.buflen) {
			throw runtime_error("requested length exceeds buffer size ("
					+ to_string(cfg.buflen) + " b)");
		}

		m_dump_offset = offset;
		m_dump_length = length;
		//m_reader_func = m_space.get_read_func(m_intf->id());

		uint32_t kseg1 = profile->kseg1();
		m_loadaddr = kseg1 | cfg.loadaddr;

		// FIXME check whether we have a custom dumpcode file
		if (true) {
			m_code = string(reinterpret_cast<const char*>(dumpcode), sizeof(dumpcode));
			m_entry = 0x4c;

			patch32(m_code, 0x10, 0);
			patch32(m_code, 0x14, kseg1 | cfg.buffer);
			patch32(m_code, 0x18, offset);
			patch32(m_code, 0x1c, length);
			patch32(m_code, 0x20, limits_read().max);
			patch32(m_code, 0x24, kseg1 | cfg.printf);

			if (m_reader_func && m_reader_func->addr) {
				patch32(m_code, 0x0c, m_reader_func->mode);
				patch32(m_code, 0x28, kseg1 | m_reader_func->addr);

				for (unsigned i = 0; i < BCM2_PATCH_NUM; ++i) {
					uint32_t offset = 0x2c + (8 * i);
					uint32_t addr = m_reader_func->patch[i].addr;
					patch32(m_code, offset, addr ? (kseg1 | addr) : 0);
					patch32(m_code, offset + 4, addr ? m_reader_func->patch[i].word : 0);
				}
			}

			uint32_t codesize = m_code.size();
			if (mipsasm_resolve_labels(reinterpret_cast<uint32_t*>(&m_code[0]), &codesize, m_entry) != 0) {
				throw runtime_error("failed to resolve mips asm labels");
			}

			m_code.resize(codesize);
			uint32_t expected = calc_checksum(m_code.substr(m_entry, m_code.size() - 4 - m_entry));
			uint32_t actual = ntohl(extract<uint32_t>(m_ram->read(m_loadaddr + m_code.size() - 4, 4)));
			bool quick = (expected == actual);

			patch32(m_code, codesize - 4, expected);

			progress pg;
			progress_init(&pg, m_loadaddr, m_code.size());

			if (m_listener && !quick) {
				printf("updating dump code at 0x%08x (%u b)\n", m_loadaddr, codesize);
			}

			for (unsigned pass = 0; pass < 2; ++pass) {
				string ramcode = m_ram->read(m_loadaddr, m_code.size());
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
						m_ram->write(m_loadaddr + i, m_code.substr(i, 4));
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

	const bcm2_func* m_reader_func = nullptr;

	reader::sp m_ram;
};

template<class T> reader::sp create_reader(const interface::sp& intf)
{
	reader::sp ret = make_shared<T>();
	ret->set_interface(intf);
	return ret;
}
}

volatile sig_atomic_t reader::s_sigint = 0;

void reader::require_capability(unsigned cap)
{
	if (capabilities() & cap) {
		return;
	}

	string name;
	switch (cap) {
	case cap_read:
		name = "read";
		break;
	case cap_write:
		name = "write";
		break;
	case cap_exec:
		name = "exec";
		break;
	default:
		name = "(unknown)";
	}

	throw runtime_error("operation requires capability " + name);
}

void reader::exec(uint32_t offset)
{
	require_capability(cap_exec);
	if (!exec_impl(offset)) {
		throw runtime_error("failed to execute function at offset 0x" + to_hex(offset));
	}
}

void reader::dump(uint32_t offset, uint32_t length, std::ostream& os)
{
	auto cleaner = make_cleaner();
	uint32_t offset_r = align_left(offset, limits_read().alignment);
	uint32_t length_r = align_right(length + (offset - offset_r), limits_read().min);
	uint32_t length_w = length;

	logger::v() << "dump: (0x" << to_hex(offset) << ", " << length << ") -> "
			<< "(0x" << to_hex(offset_r) << ", " << length_r << ")" << endl;

	do_init(offset_r, length_r, false);

	while (length_r) {
		throw_if_interrupted();

		uint32_t n = min(length_r, limits_read().max);
		string chunk = read_chunk(offset_r, n);

		if (offset_r > (offset + length)) {
			update_progress(offset + length - 2, 0);
		} else if (offset_r < offset){
			//update_progress(0, 0);
		} else {
			//update_progress(offset - offset_r, n);
		}

		if (chunk.size() != n) {
			throw runtime_error("unexpected chunk length: " + to_string(chunk.size()));
		}

		throw_if_interrupted();

		if (offset_r < offset && (offset_r + n) >= offset) {
			uint32_t pos = offset - offset_r;
			os.write(chunk.data() + pos, chunk.size() - pos);
		} else if (offset_r >= offset && length_w) {
			os.write(chunk.c_str(), min(n, length_w));
		}

		length_w = (length_w >= n) ? length_w - n : 0;
		length_r -= n;
		offset_r += n;
	}
}

void reader::dump(const addrspace::part& partition, ostream& os, uint32_t length)
{
	set_partition(partition);
	dump(partition.offset(), !length ? partition.size() : length, os);
}

string reader::read(uint32_t offset, uint32_t length)
{
	ostringstream ostr;
	dump(offset, length, ostr);
	return ostr.str();
}

void reader::write(uint32_t offset, std::istream& is, uint32_t length)
{
	if (!length) {
		auto cur = is.tellg();
		is.seekg(0, ios_base::end);
		if (is.tellg() <= cur) {
			throw runtime_error("failed to determine length of stream");
		}

		length = is.tellg() - cur;
		is.seekg(cur, ios_base::beg);
	}

	string buf;
	buf.reserve(length);
	if (is.readsome(&buf[0], length) != length) {
		throw runtime_error("failed to read " + to_string(length) + " bytes");
	}

	write(offset, buf);
}

void reader::write(uint32_t offset, const string& buf, uint32_t length)
{
	if (!length) {
		length = buf.size();
	}
	
	limits lim = limits_write();

	uint32_t offset_w = align_left(offset, lim.min);
	uint32_t length_w = align_right(length + (offset - offset_w), lim.min);

	logger::v() << "write: (0x" << to_hex(offset) << ", " << length << ") -> "
			<< "(0x" << to_hex(offset_w) << ", " << length_w << ")" << endl;

	auto cleaner = make_cleaner();
	do_init(offset_w, length_w, true);
	update_progress(offset_w, length_w, true);

	string buf_w;

	if (offset_w != offset) {
		buf_w += read(offset_w, offset - offset_w);
	}

	buf_w += buf;

	if (length_w != length) {
		buf_w += read(offset + buf.size(), length_w - length);
	}

	while (length_w) {
		uint32_t n = length_w < lim.max ? lim.min : lim.max;
		if (!write_chunk(offset_w, buf_w.substr(buf_w.size() - length_w, n))) {
			throw runtime_error("failed to write chunk (0x" + to_hex(offset) + ", " + to_string(n) + ")");
		}

		if (offset_w < offset) {
			update_progress(0, 0);
		} else if (offset_w >= (offset + length)) {
			// TODO this forces 99.99%, but it's ugly as hell!
			update_progress(offset + length - 2, 0);
		} else {
			update_progress(offset_w, n);
		}

		offset_w += n;
		length_w -= n;
	}
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


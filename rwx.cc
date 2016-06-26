#include <arpa/inet.h>
#include <iostream>
#include <fstream>
#include "bcm2dump.h"
#include "mipsasm.h"
#include "util.h"
#include "rwx.h"
#include "ps.h"

#define BFC_FLASH_READ_DIRECT

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

uint32_t parse_num(const string& str)
{
	uint32_t mult = 1;
	uint32_t base = 10;

	if (!str.empty()) {
		if (str.size() > 2 && str.substr(0, 2) == "0x") {
			base = 16;
		} else {
			switch (str.back()) {
			case 'k':
			case 'K':
				mult = 1024;
				break;
			case 'm':
			case 'M':
				mult = 1024 * 1024;
				break;
			}
		}
	}

	return lexical_cast<uint32_t>(str, base) * mult;
}

// rwx base class for a command line interface where you
// enter a command which in turn displays a (hex) dump of the
// data. for now, all rwx implementations are based on this
// class.
class parsing_rwx : public rwx
{
	public:
	virtual ~parsing_rwx() {}

	unsigned capabilities() const override
	{ return cap_read; }

	protected:
	virtual string read_chunk(uint32_t offset, uint32_t length) override final
	{
		return read_chunk_impl(offset, length, 0);
	}

	virtual string read_special(uint32_t offset, uint32_t length) override;

	virtual unsigned chunk_timeout(uint32_t offset, uint32_t length) const
	{ return 0; }

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

string parsing_rwx::read_special(uint32_t offset, uint32_t length)
{
	require_capability(cap_special);

	string buf = read_chunk(0, 0);
	if (offset >= buf.size()) {
		return "";
	} else if (!length) {
		return buf.substr(offset);
	} else {
		return buf.substr(offset, min(buf.size(), string::size_type(length)));
	}
}

string parsing_rwx::read_chunk_impl(uint32_t offset, uint32_t length, uint32_t retries)
{
	do_read_chunk(offset, length);

	string line, linebuf, chunk, last;
	uint32_t pos = offset;
	clock_t start = clock();
	unsigned timeout = chunk_timeout(offset, length);

	do {
		while ((!length || chunk.size() < length) && m_intf->pending(500)) {
			throw_if_interrupted();

			line = trim(m_intf->readln());

			if (is_ignorable_line(line)) {
				continue;
			} else {
				// no need for the timeout anymore, because we have the chunk line
				timeout = 0;

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
	} while (timeout && elapsed_millis(start) < timeout);

	if (length && (chunk.size() != length)) {
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

class bfc_ram : public parsing_rwx
{
	public:
	virtual ~bfc_ram() {}

	virtual limits limits_read() const override
	{ return limits(4, 16, 8192); }

	virtual limits limits_write() const override
	{ return limits(4, 1, 4); }

	unsigned capabilities() const override
	{ return m_space.is_ram() ? cap_rwx : (cap_read | (m_space.is_writable() ? cap_write : 0)); }

	protected:
	virtual bool exec_impl(uint32_t offset) override;
	virtual bool write_chunk(uint32_t offset, const string& chunk) override;
	virtual void do_read_chunk(uint32_t offset, uint32_t length) override;
	virtual bool is_ignorable_line(const string& line) override;
	virtual string parse_chunk_line(const string& line, uint32_t offset) override;

	virtual void init(uint32_t offset, uint32_t length, bool write) override;

	private:
	bool m_hint_decimal = false;
	bool m_rooted = true;
	bool m_check_root = true;
};

bool bfc_ram::exec_impl(uint32_t offset)
{
	return m_intf->runcmd("/call func -a 0x" + to_hex(offset), "Calling function 0x");
};

bool bfc_ram::write_chunk(uint32_t offset, const string& chunk)
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

void bfc_ram::do_read_chunk(uint32_t offset, uint32_t length)
{
	if (m_rooted) {
		m_intf->runcmd("/read_memory -s 4 -n " + to_string(length) + " 0x" + to_hex(offset));
	} else {
		m_intf->runcmd("/system/diag readmem -s 4 -n " + to_string(length) + " 0x" + to_hex(offset));
	}

	m_hint_decimal = false;
}

bool bfc_ram::is_ignorable_line(const string& line)
{
	if (line.size() >= 51) {
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

string bfc_ram::parse_chunk_line(const string& line, uint32_t offset)
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

void bfc_ram::init(uint32_t offset, uint32_t length, bool write)
{
	parsing_rwx::init(offset, length, write);

	if (m_check_root) {
		m_intf->runcmd("cd /");
		while (m_intf->pending()) {
			string line = m_intf->readln();
			if (is_bfc_prompt(line, "Console")) {
				m_rooted = false;
			}
		}
		m_check_root = false;
	}
}

class bfc_flash : public parsing_rwx
{
	public:
	virtual ~bfc_flash() {}

	virtual limits limits_read() const override
	{ return limits(1, 16, 512); }

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
		parsing_rwx::update_progress(m_partition.offset() + offset, length, init);
	}

	private:
	uint32_t to_partition_offset(uint32_t offset);
};

void bfc_flash::init(uint32_t offset, uint32_t length, bool write)
{
	if (m_partition.name().empty()) {
		throw runtime_error("no partition name argument");
	}

	for (unsigned pass = 0; pass < 2; ++pass) {
		m_intf->runcmd("/flash/open " + m_partition.altname());

		bool opened = false;
		bool retry = false;

		while (m_intf->pending()) {
			string line = m_intf->readln();
			if (contains(line, "opened twice")) {
				retry = true;
				opened = false;
			} else if (contains(line, "driver opened")) {
				opened = true;
			}
		}

		if (opened) {
			break;
		} else if (retry && pass == 0) {
			logger::d() << "reinitializing flash driver before reopening" << endl;
			cleanup();
			m_intf->runcmd("/flash/deinit", "Deinitializing");
			m_intf->runcmd("/flash/init", "Initializing");
			sleep(1);
		} else {
			throw runtime_error("failed to open partition " + m_partition.name());
		}
	}
}

void bfc_flash::cleanup()
{
	m_intf->runcmd("/flash/close", "driver closed");
}

bool bfc_flash::write_chunk(uint32_t offset, const std::string& chunk)
{
	offset = to_partition_offset(offset);
	uint32_t val = chunk.size() == 4 ? ntohl(extract<uint32_t>(chunk)) : chunk[0];
	return m_intf->runcmd("/flash/write " + to_string(chunk.size()) + " 0x"
			+ to_hex(offset) + " 0x" + to_hex(val), "successfully written");
}

void bfc_flash::do_read_chunk(uint32_t offset, uint32_t length)
{
	offset = to_partition_offset(offset);
#ifdef BFC_FLASH_READ_DIRECT
	m_intf->runcmd("/flash/readDirect " + to_string(length) + " " + to_string(offset));
#else
	m_intf->runcmd("/flash/read 4 " + to_string(length) + " " + to_string(offset));
#endif
}

bool bfc_flash::is_ignorable_line(const string& line)
{
#ifdef BFC_FLASH_READ_DIRECT
	if (line.size() >= 53) {
		if (line.substr(11, 3) == "   " && line.substr(25, 3) == "   ") {
			return false;
		}
	}	
#else
	if (line.size() >= 36) {
		if (line[8] == ' ' && line[17] == ' ' && line[26] == ' ') {
			return false;
		}
	}
#endif

	return true;
}

string bfc_flash::parse_chunk_line(const string& line, uint32_t offset)
{
	string linebuf;

#ifdef BFC_FLASH_READ_DIRECT
	for (unsigned i = 0; i < 16; ++i) {
		// don't change this to uint8_t
		uint32_t val = hex_cast<uint32_t>(line.substr(i * 3 + (i / 4) * 2, 2));
		if (val > 0xff) {
			throw runtime_error("value out of range: 0x" + to_hex(val));
		}

		linebuf += char(val);
	}
#else
	for (size_t i = 0; i < line.size(); i += 9) {
		linebuf += to_buf(htonl(hex_cast<uint32_t>(line.substr(i, 8))));
	}
#endif

	return linebuf;
}

uint32_t bfc_flash::to_partition_offset(uint32_t offset)
{
	if (offset < m_partition.offset()) {
		// just to be safe. this should never happen
		throw runtime_error("offset 0x" + to_hex(offset) + " is less than partition offset");
	}

	return offset - m_partition.offset();
}

class bootloader_ram : public parsing_rwx
{
	public:
	virtual ~bootloader_ram() {}

	virtual limits limits_read() const override
	{ return limits(4, 4, 4); }

	virtual limits limits_write() const override
	{ return limits(4, 4, 4); }

	virtual unsigned capabilities() const override
	{ return cap_rwx; }

	protected:
	virtual void init(uint32_t offset, uint32_t length, bool write) override;
	virtual void cleanup() override;

	virtual bool write_chunk(uint32_t offset, const string& chunk) override;
	virtual bool exec_impl(uint32_t offset) override;

	virtual void do_read_chunk(uint32_t offset, uint32_t length) override;
	virtual bool is_ignorable_line(const string& line) override;
	virtual string parse_chunk_line(const string& line, uint32_t offset) override;
};

void bootloader_ram::init(uint32_t offset, uint32_t length, bool write)
{
	if (!write) {
		m_intf->runcmd("r");
	}
}

void bootloader_ram::cleanup()
{
	m_intf->writeln();
	m_intf->writeln();
}

bool bootloader_ram::write_chunk(uint32_t offset, const string& chunk)
{
	try {
		m_intf->writeln();

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

void bootloader_ram::do_read_chunk(uint32_t offset, uint32_t length)
{
	m_intf->writeln("0x" + to_hex(offset));
}

bool bootloader_ram::is_ignorable_line(const string& line)
{
	if (contains(line, "Value at") || contains(line, "(hex)")) {
		return false;
	}

	return true;
}

string bootloader_ram::parse_chunk_line(const string& line, uint32_t offset)
{
	if (line.find("Value at") == 0) {
		if (offset != hex_cast<uint32_t>(line.substr(9, 8))) {
			throw runtime_error("offset mismatch");
		}

		return to_buf(htonl(hex_cast<uint32_t>(line.substr(19, 8))));
	}

	throw runtime_error("unexpected line");
}

bool bootloader_ram::exec_impl(uint32_t offset)
{
	m_intf->runcmd("");
	m_intf->runcmd("j", "");
	m_intf->writeln(to_hex(offset));
	// FIXME
	return true;
}

// this defines uint32 dumpcode[]
#include "dumpcode.h"

class dumpcode_rwx : public parsing_rwx
{
	public:
	dumpcode_rwx() {}
	dumpcode_rwx(const func& func) : m_read_func(func) {}

	virtual limits limits_read() const override
	{ return limits(4, 16, 0x4000); }

	virtual limits limits_write() const override
	{ return limits(); }

	virtual void set_interface(const interface::sp& intf) override
	{
		parsing_rwx::set_interface(intf);

		if (!intf->profile()) {
			throw runtime_error("dumpcode requires a profile");
		}

		const codecfg& cfg = intf->profile()->codecfg(intf->id());

		if (!cfg.loadaddr || !cfg.buffer || !cfg.printf) {
			throw runtime_error("insufficient profile infos for dumpcode");
		} else if (cfg.loadaddr & 0xffff) {
			throw runtime_error("loadaddr must be aligned to 64k");
		}
		m_ram = rwx::create(intf, "ram");
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

		auto values = split(line.substr(1), ':');
		if (values.size() == 4) {
			for (string val : values) {
				linebuf += to_buf(htonl(hex_cast<uint32_t>(val)));
			}
		}

		return linebuf;
	}

	protected:
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

	unsigned chunk_timeout(uint32_t offset, uint32_t length) const override
	{
		if (offset != m_dump_offset || !m_read_func.addr()) {
			return parsing_rwx::chunk_timeout(offset, length);
		} else {
			return 60 * 1000;
		}
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
		//m_rwx_func = m_space.get_read_func(m_intf->id());

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

			if (m_read_func.addr()) {
				patch32(m_code, 0x0c, m_read_func.args());
				patch32(m_code, 0x28, kseg1 | m_read_func.addr());

				unsigned i = 0;
				for (auto patch : m_read_func.patches()) {
					uint32_t offset = 0x2c + (8 * i++);
					uint32_t addr = patch->addr;
					patch32(m_code, offset, addr ? (kseg1 | addr) : 0);
					patch32(m_code, offset + 4, addr ? patch->word : 0);
				}
			}

			uint32_t codesize = m_code.size();
			if (mipsasm_resolve_labels(reinterpret_cast<uint32_t*>(&m_code[0]), &codesize, m_entry) != 0) {
				throw runtime_error("failed to resolve mips asm labels");
			}

			m_code.resize(codesize);
			uint32_t expected = 0xc0de0000 | crc16_ccitt(m_code.substr(m_entry, m_code.size() - 4 - m_entry));
			uint32_t actual = ntohl(extract<uint32_t>(m_ram->read(m_loadaddr + m_code.size() - 4, 4)));
			bool quick = (expected == actual);

			patch32(m_code, codesize - 4, expected);

			progress pg;
			progress_init(&pg, m_loadaddr, m_code.size());

			if (m_prog_l && !quick) {
				printf("updating dump code at 0x%08x (%u b)\n", m_loadaddr, codesize);
			}

			for (unsigned pass = 0; pass < 2; ++pass) {
				string ramcode = m_ram->read(m_loadaddr, m_code.size());
				for (uint32_t i = 0; i < m_code.size(); i += 4) {
					if (!quick && pass == 0 && m_prog_l) {
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

				if (!quick && pass == 0 && m_prog_l) {
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

	func m_read_func;

	rwx::sp m_ram;
};

template<class T> rwx::sp create_rwx(const interface::sp& intf, const addrspace& space)
{
	auto ret = make_shared<T>();
	ret->set_interface(intf);
	ret->set_addrspace(space);
	return ret;
}

rwx::sp create_dumpcode_rwx(const interface::sp& intf, const addrspace& space)
{
	rwx::sp ret = make_shared<dumpcode_rwx>(space.get_read_func(intf->id()));
	ret->set_interface(intf);
	ret->set_addrspace(space);
	return ret;
}

class bfc_cmcfg : public parsing_rwx
{
	public:
	virtual ~bfc_cmcfg() {}

	virtual limits limits_read() const override
	{ return limits(1); }

	virtual limits limits_write() const override
	{ return limits(); }

	unsigned capabilities() const override
	{ return cap_read | cap_special; }

	protected:
	virtual void do_read_chunk(uint32_t offset, uint32_t length) override;
	virtual bool is_ignorable_line(const string& line) override;
	virtual string parse_chunk_line(const string& line, uint32_t offset) override;

	virtual string read_special(uint32_t offset, uint32_t length) override
	{ return parsing_rwx::read_special(offset, length) + "\xff"; }

	private:
	bool m_hint_decimal = false;
	bool m_rooted = true;
};

void bfc_cmcfg::do_read_chunk(uint32_t offset, uint32_t length)
{
	m_intf->runcmd("/docsis_ctl/cfg_hex_show");
}

bool bfc_cmcfg::is_ignorable_line(const string& line)
{
	//bool ret = line.size() != 75 || line.substr(55, 4) != "  | ";
	bool ret = line.size() < 58 || line.size() > 73 || line.substr(53, 4) != "  | ";
	return ret;
}

string bfc_cmcfg::parse_chunk_line(const string& line, uint32_t offset)
{
	string linebuf;
	for (unsigned i = 0; i < 16; ++i) {
		unsigned offset = 2 * (i / 4) + 3 * i;
		if (offset > line.size() || offset + 2 > line.size()) {
			break;
		}

		try {
			linebuf += hex_cast<int>(line.substr(offset, 2));
		} catch (const bad_lexical_cast& e) {
			if (line.size() == 73) {
				throw e;
			}
		}
	}

	return linebuf;
}
}

unsigned rwx::s_count = 0;
sighandler_t rwx::s_sighandler_orig = nullptr;
volatile sig_atomic_t rwx::s_sigint = 0;

rwx::rwx()
{
	if (++s_count == 1) {
		s_sighandler_orig = signal(SIGINT, &rwx::handle_sigint);
	}
}

rwx::~rwx()
{
	if (--s_count == 0) {
		signal(SIGINT, s_sighandler_orig);
	}
}

void rwx::require_capability(unsigned cap)
{
	if ((capabilities() & cap) == cap) {
		return;
	}

	string name;
	switch (cap & cap_rwx) {
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

	throw runtime_error("operation requires capability " + name + ((cap & cap_special) ? " special" : ""));
}

void rwx::exec(uint32_t offset)
{
	require_capability(cap_exec);
	if (!exec_impl(offset)) {
		throw runtime_error("failed to execute function at offset 0x" + to_hex(offset));
	}
}

void rwx::dump(uint32_t offset, uint32_t length, std::ostream& os)
{
	require_capability(cap_read);

	auto cleaner = make_cleaner();

	if (capabilities() & cap_special) {
		do_init(0, 0, false);
		update_progress(0, 0, true);
		read_special(offset, length, os);
		return;
	} else {
		m_space.check_range(offset, length);
	}

	uint32_t offset_r = align_left(offset, limits_read().alignment);
	uint32_t length_r = align_right(length + (offset - offset_r), limits_read().min);
	uint32_t length_w = length;

	do_init(offset_r, length_r, false);
	update_progress(offset_r, length_r, true);

	string hdrbuf;
	bool show_hdr = true;

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

		string chunk_w;

		if (offset_r < offset && (offset_r + n) >= offset) {
			uint32_t pos = offset - offset_r;
			chunk_w = string(chunk.c_str() + pos, chunk.size() - pos);
			//os.write(chunk.data() + pos, chunk.size() - pos);
		} else if (offset_r >= offset && length_w) {
			chunk_w = string(chunk.c_str(), min(n, length_w));
		}

		os.write(chunk_w.data(), chunk_w.size());

		if (show_hdr) {
			if (hdrbuf.size() < sizeof(ps_header)) {
				hdrbuf += chunk_w;
			}

			if (hdrbuf.size() >= sizeof(ps_header)) {
				ps_header hdr(hdrbuf);

				if (hdr.hcs_valid()) {
					image_detected(offset, hdr);
				}

				show_hdr = false;
			}
		}

		length_w = (length_w >= n) ? length_w - n : 0;
		length_r -= n;
		offset_r += n;
	}
}

void rwx::dump(const string& spec, ostream& os)
{
	vector<string> tokens = split(spec, ',');
	if (tokens.empty() || tokens.size() > 2) {
		throw invalid_argument("invalid argument: '" + spec + "'");
	}

	uint32_t offset = 0;
	uint32_t length = 0;

	try {
		offset = parse_num(tokens[0]);
		if (tokens.size() < 2) {
			throw invalid_argument("missing size argument");
		}
		length = parse_num(tokens[1]);
	} catch (const bad_lexical_cast& e) {
		if (offset) {
			throw e;
		}

		const addrspace::part& p = m_space.partition(tokens[0]);
		set_partition(p);
		offset = p.offset();
		length = tokens.size() >= 2 ? parse_num(tokens[1]) : p.size();
	}

	return dump(offset, length, os);
}

string rwx::read(uint32_t offset, uint32_t length)
{
	ostringstream ostr;
	dump(offset, length, ostr);
	return ostr.str();
}

void rwx::write(const string& spec, istream& is)
{
	vector<string> tokens = split(spec, ',');
	if (tokens.empty() || tokens.size() > 2) {
		throw invalid_argument("invalid argument: '" + spec + "'");
	}

	uint32_t offset = 0;
	uint32_t length = 0;

	if (!tokens.empty()) {
		try {
			offset = parse_num(tokens[0]);
		} catch (const bad_lexical_cast& e) {
			const addrspace::part& p = m_space.partition(tokens[0]);
			set_partition(p);
			offset = p.offset();
		}
	}

	if (tokens.size() == 2) {
		length = parse_num(tokens[1]);
	}

	write(offset, is, length);
}

void rwx::write(uint32_t offset, istream& is, uint32_t length)
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
	buf.resize(length);
	if (is.readsome(&buf[0], length) != length) {
		throw runtime_error("failed to read " + to_string(length) + " bytes");
	}

	write(offset, buf);
}

void rwx::write(uint32_t offset, const string& buf, uint32_t length)
{
	if (!length) {
		length = buf.size();
	}
	
	m_space.check_range(offset, length);

	limits lim = limits_write();

	uint32_t offset_w = align_left(offset, lim.min);
	uint32_t length_w = align_right(length + (offset - offset_w), lim.min);

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

void rwx::read_special(uint32_t offset, uint32_t length, ostream& os)
{
	string buf = read_special(offset, length);
	os.write(buf.data(), buf.size());
}

// TODO this should be migrated to something like
// interface::create_rwx(const string& type)
rwx::sp rwx::create(const interface::sp& intf, const string& type, bool safe)
{
	addrspace space;
	if (intf->profile()) {
		space = intf->profile()->space(type, intf->id());
	} else if (type == "ram") {
		space = profile::get("generic")->ram();
		safe = true;
	} else {
		throw invalid_argument("cannot create non-ram rwx object without a profile");
	}

	if (intf->name() == "bootloader") {
		if (space.is_mem()) {
			if (safe) {
				return create_rwx<bootloader_ram>(intf, space);
			} else {
				return create_dumpcode_rwx(intf, space);
			}
		} else {
			return create_dumpcode_rwx(intf, space);
		}
	} else if (intf->name() == "bfc") {
		safe = true;
		if (safe) {
			if (space.is_mem()) {
				return create_rwx<bfc_ram>(intf, space);
			} else {
				return create_rwx<bfc_flash>(intf, space);
			}
		}
	}

	throw invalid_argument("no such rwx: " + intf->name() + "," + type + ((safe ? "" : ",un") + string("safe")));
}

rwx::sp rwx::create_special(const interface::sp& intf, const string& type)
{
	if (intf->name() == "bfc") {
		if (type == "cmcfg") {
			auto rwx = make_shared<bfc_cmcfg>();
			rwx->set_interface(intf);
			return rwx;
		}
	}

	throw invalid_argument("no such special rwx: " + intf->name() + "," + type);
}
}


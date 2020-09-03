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

#include <unistd.h>
#include <iostream>
#include <cstddef>
#include <fstream>
#include "progress.h"
#include "rwcode2.h"
#include "util.h"
#include "rwx.h"
#include "ps.h"

#define BFC_FLASH_READ_DIRECT

using namespace std;

namespace bcm2dump {
namespace {

class bad_chunk_line : public runtime_error
{
	bool m_critical;

	public:
	template<class T> static bad_chunk_line critical(const T& t)
	{ return { t, true }; }

	template<class T> static bad_chunk_line regular(const T& t)
	{ return { t, false }; }

	static bad_chunk_line regular()
	{ return { "bad_chunk_line", false }; }

	bool critical() const
	{ return m_critical; }

	private:
	bad_chunk_line(const exception& cause, bool critical)
	: runtime_error(cause.what()), m_critical(critical)
	{}

	bad_chunk_line(const string& what, bool critical)
	: runtime_error(what), m_critical(critical)
	{}
};

const unsigned max_retry_count = 5;

template<class T> T hex_cast(const std::string& str)
{
	return lexical_cast<T>(str, 16);
}

inline void patch32(string& buf, string::size_type offset, uint32_t n)
{
	patch<uint32_t>(buf, offset, hton(n));
}

template<class T> T align_to(const T& num, const T& alignment)
{
	if (num % alignment) {
		return ((num / alignment) + 1) * alignment;
	}

	return num;
}

streampos tell(ostream& os)
{
	return os.tellp();
}

streampos tell(istream& is)
{
	return is.tellg();
}

void seek(istream& is, streamoff off, ios_base::seekdir dir)
{
	is.seekg(off, dir);
}

void seek(ostream& os, streamoff off, ios_base::seekdir dir)
{
	os.seekp(off, dir);
}

template<class T> uint32_t get_stream_size(T& stream)
{
	auto ioex = scoped_ios_exceptions::none(stream);
	auto cur = tell(stream);
	if (cur == -1) {
		return 0;
	}

	seek(stream, 0, ios_base::end);
	if (!stream.good() || tell(stream) <= cur) {
		throw runtime_error("failed to determine length of stream");
	}

	uint32_t length = tell(stream) - cur;
	seek(stream, cur, ios_base::beg);
	return length;
}

uint32_t parse_num(const string& str)
{
	return lexical_cast<uint32_t>(str, 0);
}

uint32_t read_image_length(rwx& rwx, uint32_t offset)
{
	rwx.silent(true);
	ps_header hdr(rwx.read(offset, 92));
	rwx.silent(false);
	return hdr.hcs_valid() ? 92 + hdr.length() : 0;
}

void parse_offset_size(rwx& rwx, const string& arg, uint32_t& offset, uint32_t& length, bool write)
{
	auto tokens = split(arg, ',');
	if (tokens.empty() || tokens.size() > 2) {
		throw user_error("invalid argument '" + arg + "'");
	}

	bool read_hdr = false;
	offset = 0;
	length = 0;

	if (tokens.size() == 2) {
		if (tokens[1] != "auto") {
			length = parse_num(tokens[1]);
		} else if (!write) {
			read_hdr = true;
		}
	}

	try {
		offset = parse_num(tokens[0]);
		if (!length && !write) {
			length = read_image_length(rwx, offset);
		}
	} catch (const bad_lexical_cast& e) {
		tokens = split(tokens[0], '+');
		if (tokens.empty() || tokens.size() > 2) {
			throw user_error("invalid argument '" + arg + "'");
		}

		const addrspace::part& p = rwx.space().partition(tokens[0]);
		rwx.set_partition(p);
		offset = p.offset();
		if (!length && !read_hdr) {
			length = p.size();
		}

		if (!length && !write) {
			length = read_image_length(rwx, offset);
		}

		if (!write && !length && !p.size()) {
			throw user_error("size of partition '" + p.name() + "' is unknown, and size argument is missing");
		}

		if (tokens.size() == 2) {
			uint32_t n = parse_num(tokens[1]);
			offset += n;

			if (!write && !length) {
				length = p.size() - n;
			}
		} else if (!length && !write) {
			length = p.size();
		}
	}
}

bool wait_for_interface(const interface::sp& intf)
{
	for (unsigned i = 0; i < 10; ++i) {
		if (intf->is_ready(false)) {
			return true;
		} else if (i != 9) {
			sleep(1);
		}
	}

	return false;
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

	uint32_t pos = offset;
	string chunk;

	m_intf->foreach_line([this, &chunk, &pos, &retries] (const string& line) {
		throw_if_interrupted();
		string tline = trim(line);
		if (!is_ignorable_line(line)) {
			try {
				string linebuf = parse_chunk_line(line, pos);
				pos += linebuf.size();
				chunk += linebuf;
				update_progress(pos, chunk.size());
			} catch (const bad_chunk_line& e) {
				string msg = "bad chunk line @" + to_hex(pos) + ": '" + line + "' (" + e.what() + ")";
				if (e.critical() && retries >= max_retry_count) {
					throw runtime_error(msg);
				}

				logger::d() << endl << msg << endl;
			} catch (const exception& e) {
				logger::d() << "error while parsing '" << line << "': " << e.what() << endl;
				return true;
			}
		}

		return line.empty();
	}, chunk_timeout(offset, length));

	if (length && (chunk.size() != length)) {
		string msg = "read incomplete chunk 0x" + to_hex(offset)
					+ ": " + to_string(chunk.size()) + "/" +to_string(length);
		if (retries < max_retry_count) {
			// if the dump is still underway, we need to wait for it to finish
			// before issuing the next command. wait for up to 10 seconds.

			if (wait_for_interface(m_intf)) {
				logger::d() << endl << msg << "; retrying" << endl;
				on_chunk_retry(offset, length);
				return read_chunk_impl(offset, length, retries + 1);
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
	{
		if (m_intf->is_privileged()) {
			return limits(4, 1, 4);
		} else {
			return limits(1, 1, 1);
		}
	}

	unsigned capabilities() const override
	{ return m_space.is_ram() ? cap_rwx : (cap_read | (m_space.is_writable() ? cap_write : 0)); }

	protected:
	virtual bool exec_impl(uint32_t offset) override;
	virtual bool write_chunk(uint32_t offset, const string& chunk) override;
	virtual bool is_ignorable_line(const string& line) override;
	virtual void do_read_chunk(uint32_t offset, uint32_t length) override;
	virtual string parse_chunk_line(const string& line, uint32_t offset) override;

	private:
	bool m_hint_decimal = false;
};

bool bfc_ram::exec_impl(uint32_t offset)
{
	return m_intf->run("/call func -a 0x" + to_hex(offset), "Calling function 0x");
};

bool bfc_ram::write_chunk(uint32_t offset, const string& chunk)
{
	if (m_intf->is_privileged()) {
		uint32_t val = chunk.size() == 4 ? ntoh(extract<uint32_t>(chunk)) : chunk[0];
		return m_intf->run("/write_memory -s " + to_string(chunk.size()) + " 0x" +
				to_hex(offset, 0) + " 0x" + to_hex(val, 0), "Writing");
	} else {
		// diag writemem only supports writing bytes
		return m_intf->run("/system/diag writemem 0x" + to_hex(offset, 0) + " 0x" +
				to_hex(chunk[0] & 0xff, 0), "Writing");
	}
}

void bfc_ram::do_read_chunk(uint32_t offset, uint32_t length)
{
	if (m_intf->is_privileged()) {
		m_intf->writeln("/read_memory -s 4 -n " + to_string(length) + " 0x" + to_hex(offset));
	} else {
		m_intf->writeln("/system/diag readmem -s 4 -n " + to_string(length) + " 0x" + to_hex(offset));
	}

	m_hint_decimal = false;
}

bool bfc_ram::is_ignorable_line(const string& line)
{
	if (line.size() >= 50) {
		if (line.substr(8, 2) == ": " && line.substr(48, 2) == " |") {
			m_hint_decimal = false;
			return false;
		} else if (contains(line, ": ") && (contains(line, " | ") || ends_with(line, " |"))) {
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
	uint32_t data[4];
	uint32_t off;

	int n = sscanf(line.c_str(),
			m_hint_decimal ? "%u: %u  %u  %u  %u" : "%x: %x  %x  %x  %x",
			&off, &data[0], &data[1], &data[2], &data[3]);

	if (!n) {
		throw bad_chunk_line::regular();
	} else if (off != offset) {
		throw bad_chunk_line::critical("offset mismatch");
	}

	string linebuf;

	for (int i = 0; i < (n - 1); ++i) {
		linebuf += to_buf(ntoh(data[i]));
	}

	return linebuf;

}

class bfc_flash2 : public bfc_ram
{
	public:
	virtual ~bfc_flash2() {}

	virtual unsigned capabilities() const override
	{ return cap_read; }

	static bool is_supported(const interface::sp& intf, const string& space)
	{
		auto ver = intf->version();
		if (ver.name().empty()) {
			return false;
		}

		auto cfg = ver.codecfg();
		if (!cfg["buffer"]) {
			return false;
		}

		auto funcs = ver.functions(space);
		if (!funcs["read"].addr() /*&& !funcs["write"].addr()*/) {
			return false;
		}

		return true;
	}

	protected:
	virtual void init(uint32_t offset, uint32_t length, bool write) override
	{
		bfc_ram::init(offset, length, write);

		auto ver = m_intf->version();
		m_cfg = ver.codecfg();
		uint32_t buflen = m_cfg["buflen"];

		if (buflen && length > buflen) {
			throw user_error("requested length exceeds buffer size ("
					+ to_string(buflen) + " b)");
		}

		m_dump_offset = offset;
		m_dump_length = length;
		m_funcs = ver.functions(m_space.name());
		m_read = true;

		call_open_close("open", offset, length);
	}

	virtual void cleanup() override
	{
		bfc_ram::cleanup();
		call_open_close("close", m_dump_offset, m_dump_length);
	}

	virtual unsigned chunk_timeout(uint32_t offset, uint32_t length) const override
	{
		return offset == m_dump_offset ? 60 * 1000 : 0;
	}

	virtual string parse_chunk_line(const string& line, uint32_t offset) override
	{
		return bfc_ram::parse_chunk_line(line, m_cfg["buffer"] + (offset - m_dump_offset));
	}

	virtual void do_read_chunk(uint32_t offset, uint32_t length) override
	{
		if (m_read) {
			call_read(m_dump_offset, m_dump_length);
			m_read = false;
		}

		bfc_ram::do_read_chunk(m_cfg["buffer"] + (offset - m_dump_offset), length);
	}

	private:
	void patch(const func& f)
	{
		for (auto p : f.patches()) {
			if (p->addr && !write_chunk(p->addr | m_intf->profile()->kseg1(), to_buf(hton(p->word)))) {
				throw runtime_error("failed to patch word at 0x" + to_hex(p->addr));
			}
		}
	}

	void call(const string& cmd, const string& name, unsigned timeout = 5)
	{
		m_intf->run(cmd);
#if 0
		// consume lines
		m_intf->foreach_line([] (const string&) { return true; }, timeout * 1000, 0);
#else
		if (!m_intf->wait_ready(timeout)) {
			throw runtime_error("timeout while waiting for function '" + name + "' to finish");
		}
#endif
	}

	void call_read(uint32_t offset, uint32_t length)
	{
		func read = m_funcs["read"];
		if (read.addr()) {
			string cmd = mkcmd(read);
			if (read.args() == BCM2_READ_FUNC_BOL) {
				args(cmd, { m_cfg["buffer"], offset, length });
			} else if (read.args() == BCM2_READ_FUNC_OBL) {
				args(cmd, { offset, m_cfg["buffer"], length });
			} else {
				throw runtime_error("unsupported 'read' args");
			}

			patch(read);
			call(cmd, "read", 120);
		}
	}

	void call_open_close(const string& name, uint32_t offset, uint32_t length)
	{
		func f = m_funcs[name];
		if (f.addr()) {
			string cmd = mkcmd(f, { offset });

			if (f.args() == BCM2_ARGS_OL) {
				arg(cmd, length);
			} else if (f.args() == BCM2_ARGS_OE) {
				arg(cmd, offset + length);
			} else {
				throw runtime_error("unsupported '" + name + "' args");
			}

			patch(f);
			call(cmd, name);
		}
	}

	string mkcmd(const func& f, vector<uint32_t> a = {})
	{
		string ret = "/call func -a 0x" + to_hex(f.addr() | m_intf->profile()->kseg1());
		args(ret, a);
		return ret;
	}

	static void arg(string& cmd, uint32_t arg)
	{ cmd += " 0x" + to_hex(arg); }

	static void args(string& cmd, vector<uint32_t> args)
	{
		for (auto a : args) {
			arg(cmd, a);
		}
	}

	bool m_read = true;
	uint32_t m_dump_offset = 0;
	uint32_t m_dump_length = 0;
	version::funcmap m_funcs;
	version::u32map m_cfg;
};

class bfc_flash : public parsing_rwx
{
	public:
	virtual ~bfc_flash()
	{ cleanup(); }

	virtual limits limits_read() const override
	{
		return use_direct_read() ? limits(1, 16, 4096) : limits(1, 16, 512);
	}

	virtual limits limits_write() const override
	{ return limits(1, 1, 4); }

	protected:
	virtual void init(uint32_t offset, uint32_t length, bool write) override;
	virtual void cleanup() override;

	virtual bool write_chunk(uint32_t offset, const string& buf) override;

	virtual void do_read_chunk(uint32_t offset, uint32_t length) override;
	virtual bool is_ignorable_line(const string& line) override;
	virtual string parse_chunk_line(const string& line, uint32_t offset) override;
	virtual void on_chunk_retry(uint32_t offset, uint32_t length) override;

	private:
	uint32_t to_partition_offset(uint32_t offset) const;
	bool use_direct_read() const;
};

void bfc_flash::init(uint32_t, uint32_t, bool write)
{
	if (m_partition.name().empty()) {
		throw user_error("partition name required");
	}

	for (unsigned pass = 0; pass < 2; ++pass) {
		auto lines = m_intf->run("/flash/open " + m_partition.altname());

		bool opened = false;
		bool retry = false;

		for (auto line : lines) {
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
			m_intf->run("/flash/deinit", "Deinitializing");
			m_intf->run("/flash/init", "Initializing");
			sleep(1);
		} else {
			throw runtime_error("failed to open partition " + m_partition.name());
		}
	}
}

void bfc_flash::cleanup()
{
	m_intf->run("/flash/close", "driver closed");
}

bool bfc_flash::write_chunk(uint32_t offset, const std::string& chunk)
{
	offset = to_partition_offset(offset);
	uint32_t val = chunk.size() == 4 ? ntoh(extract<uint32_t>(chunk)) : chunk[0];
	return m_intf->run("/flash/write " + to_string(chunk.size()) + " 0x"
			+ to_hex(offset) + " 0x" + to_hex(val), "successfully written");
}

void bfc_flash::do_read_chunk(uint32_t offset, uint32_t length)
{
	offset = to_partition_offset(offset);
	if (use_direct_read()) {
		m_intf->writeln("/flash/readDirect " + to_string(length) + " " + to_string(offset));
	} else {
		m_intf->writeln("/flash/read 4 " + to_string(length) + " " + to_string(offset));
	}
}

bool bfc_flash::is_ignorable_line(const string& line)
{
	if (use_direct_read()) {
		if (line.size() >= 53) {
			if (line.substr(11, 3) == "   " && line.substr(25, 3) == "   ") {
				return false;
			}
		}
	} else if (line.size() >= 36) {
		if (line[8] == ' ' && line[17] == ' ' && line[26] == ' ') {
			return false;
		}
	}

	return true;
}

string bfc_flash::parse_chunk_line(const string& line, uint32_t offset)
{
	auto tok = split(line, ' ', false);
	if (tok.empty()) {
		throw bad_chunk_line::regular();
	}

	string linebuf;

	for (auto num : tok) {
		uint32_t n;
		try {
			n = hex_cast<uint32_t>(num);
		} catch (const exception& e) {
			throw bad_chunk_line::regular(e);
		}

		if (use_direct_read()) {
			if (n > 0xff) {
				throw bad_chunk_line::regular("invalid byte: 0x" + to_hex(n));
			}

			linebuf += char(n);
		} else {
			linebuf += to_buf(ntoh(n));
		}
	}

	return linebuf;
}

uint32_t bfc_flash::to_partition_offset(uint32_t offset) const
{
	if (offset < m_partition.offset()) {
		// just to be safe. this should never happen
		throw runtime_error("offset 0x" + to_hex(offset) + " is less than partition offset");
	}

	return offset - m_partition.offset();
}

bool bfc_flash::use_direct_read() const
{
	auto v = m_intf->version();

	if (v.has_opt("bfc:flash_read_direct")) {
		return v.get_opt_num("bfc:flash_read_direct");
	}

#ifdef BFC_FLASH_READ_DIRECT
	return true;
#else
	return false;
#endif
}

void bfc_flash::on_chunk_retry(uint32_t offset, uint32_t length)
{
	auto v = m_intf->version();

	if (v.get_opt_num("bfc:flash_reinit_on_retry", false)) {
		cleanup();
		init(0, 0, false);
	}
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
		m_intf->write("r");
	} else {
		m_intf->writeln();
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
		if (!m_intf->run("w", "Write memory.", true)) {
			return false;
		}

		m_intf->writeln(to_hex(offset, 0));
		uint32_t val = ntoh(extract<uint32_t>(chunk));
		m_intf->writeln(to_hex(val));
		return true;
	} catch (const exception& e) {
		// ensure that we're in a sane state
		m_intf->run("\r");
		return false;
	}
}

void bootloader_ram::do_read_chunk(uint32_t offset, uint32_t length)
{
	m_intf->writeln("0x" + to_hex(offset, 0));
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
			throw bad_chunk_line::critical("offset mismatch");
		}

		return to_buf(hton(hex_cast<uint32_t>(line.substr(19, 8))));
	}

	throw bad_chunk_line::regular();
}

bool bootloader_ram::exec_impl(uint32_t offset)
{
	m_intf->run("");
	m_intf->run("j", "");
	m_intf->writeln(to_hex(offset));
	// FIXME
	return true;
}

/**
 * TODO
 *
 * bootloader2 interface:
 * > r hexAddr [width]			Display memory location (1/2/4 bytes)
 * > w hexAddr hexVal [width]	Write memory location (1/2/4 bytes)
 * > d hexAddr length [width]	Dump memory
 *
 * There's no command to execute code, so we have to hijack the function
 * of another command, updating its code to
 *
 *		li $at, [jump address]
 *		jr $at
 *		nop
 *
 * A good candidate would be 'z' (Cause exception).
 */

// this defines uint32 dumpcode[] and writecode[]
#include "rwcode2.inc"

class code_rwx : public parsing_rwx
{
	public:
	code_rwx() {}

	virtual limits limits_read() const override
	{ return limits(16, 16, 0x4000); }

	virtual limits limits_write() const override
	{ return limits(8, 8, 0x4000); }

	virtual unsigned capabilities() const override
	{ return cap_rwx; }

	virtual void set_interface(const interface::sp& intf) override
	{
		parsing_rwx::set_interface(intf);

		if (!intf->profile()) {
			throw runtime_error("code dumper requires a profile");
		}

		auto cfg = intf->version().codecfg();
		if (!cfg["rwcode"] || !cfg["buffer"] || !cfg["printf"]) {
			throw runtime_error("insufficient profile information for code dumper");
		} else if (cfg["rwcode"] & 0xfff) {
			throw runtime_error("rwcode address must be aligned to 4k");
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
		auto lim = limits_read();

		if (values.size() < (lim.min / 4) || values.size() > (lim.max / 4)) {
			throw runtime_error("invalid chunk line: '" + line + "'");
		}

		for (string val : values) {
			linebuf += to_buf(hton(hex_cast<uint32_t>(val)));
		}

		return linebuf;
	}

	protected:
	virtual bool write_chunk(uint32_t offset, const string& chunk) override
	{
		m_ram->exec(m_loadaddr + m_entry);

		for (size_t i = 0; i < chunk.size(); i += limits_write().min) {
			string line;

			for (size_t k = 0; k < limits_write().min / 4; ++k) {
				line += ":" + to_hex(chunk.substr(i + k * 4, 4));
			}

			m_intf->writeln(line);

			line = trim(m_intf->readln());
			if (line.empty() || line[0] != ':') {
				throw runtime_error("expected offset, got '" + line + "'");
			}

			uint32_t actual = hex_cast<uint32_t>(line.substr(1));
			if (actual != (offset + i)) {
				throw runtime_error("expected offset 0x" + to_hex(offset + i, 8) + ", got 0x" + to_hex(actual));
			}

			update_progress(offset + i, 16);
		}

		if (!space().is_ram()) {
			// FIXME
			m_intf->wait_ready(60);
		}

		return true;
	}

	bool is_prompt_line(const string& line, uint32_t offset)
	{
		if (line.empty() || line[0] != ':') {
			return false;
		}

		if (offset != hex_cast<uint32_t>(line.substr(1))) {
			throw runtime_error("offset mismatch");
		}

		return true;
	}

	virtual bool exec_impl(uint32_t offset) override
	{
		m_ram->exec(offset);
		return true;
	}

	void on_chunk_retry(uint32_t offset, uint32_t length) override
	{
		if (false) {
			throw runtime_error("error recovery is not possible with custom dumpcode");
		}

		if (!m_write) {
			m_ram->write(m_loadaddr + offsetof(bcm2_read_args, offset), to_buf(hton(offset)));
			m_ram->write(m_loadaddr + offsetof(bcm2_read_args, length), to_buf(hton(length)));
		} else {
			// TODO: implement if we ever use on_chunk_retry for writes
		}
	}

	unsigned chunk_timeout(uint32_t offset, uint32_t length) const override
	{
		if (offset != m_rw_offset || space().is_mem()) {
			return parsing_rwx::chunk_timeout(offset, length);
		} else {
			return 60 * 1000;
		}
	}

	void init(uint32_t offset, uint32_t length, bool write) override
	{
		const profile::sp& profile = m_intf->profile();
		auto cfg = m_intf->version().codecfg();

		if (cfg["buflen"] && length > cfg["buflen"]) {
			throw user_error("requested length exceeds buffer size ("
					+ to_string(cfg["buflen"]) + " b)");
		}

#if 0
		if (write && !m_space.is_ram()) {
			throw user_error("writing to non-ram address space is not supported");
		}
#endif

		m_write = write;
		m_rw_offset = offset;
		m_rw_length = length;

		uint32_t kseg1 = profile->kseg1();
		m_loadaddr = kseg1 | (cfg["rwcode"] + (write ? 0 : 0 /*0x10000*/));

		string code;

		// TODO: check whether we have a custom code file
		if (true) {
			if (!write) {
				bcm2_read_args args = get_read_args(offset, length);
				m_entry = sizeof(args);
				code = to_buf(args);

				for (uint32_t word : mips_read_code) {
					code += to_buf(hton(word));
				}
			} else {
				bcm2_write_args args = get_write_args(offset, length);
				m_entry = sizeof(args);
				code = to_buf(args);

				for (uint32_t word : mips_write_code) {
					code += to_buf(hton(word));
				}
			}

			size_t codesize = code.size() - m_entry;

			uint32_t expected = 0xc0de0000 | crc16_ccitt(code.substr(m_entry, codesize));
			uint32_t actual = ntoh(extract<uint32_t>(m_ram->read(m_loadaddr + codesize, 4)));
			bool quick = (expected == actual);

			code += to_buf(hton(expected));

#if 1
			ofstream("code.bin").write(code.data(), code.size());
#endif

			progress pg;
			progress_init(&pg, m_loadaddr, code.size());

			if (m_prog_l && !quick) {
				logger::i("updating code at 0x%08x (%lu b)\n", m_loadaddr, code.size());
			}

			for (unsigned pass = 0; pass < 2; ++pass) {
				string ramcode = m_ram->read(m_loadaddr, code.size());
				for (uint32_t i = 0; i < code.size(); i += 4) {
					if (!quick && pass == 0 && m_prog_l) {
						progress_add(&pg, 4);
						logger::i("\r ");
						progress_print(&pg, stdout);
					}

					if (ramcode.substr(i, 4) != code.substr(i, 4)) {
						if (pass == 1) {
							throw runtime_error("dump code verification failed at 0x" + to_hex(i + m_loadaddr, 8));
						}
						m_ram->write(m_loadaddr + i, code.substr(i, 4));
					}
				}
			}

			logger::i("\n");
		}
	}

	template<size_t N> void copy_patches(bcm2_patch (&dest)[N], const func& f, uint32_t kseg1)
	{
		auto ps = f.patches();

		for (size_t i = 0; i < N && i < ps.size(); ++i) {
			if (ps[i]->addr) {
				dest[i].addr = hton(kseg1 | ps[i]->addr);
				dest[i].word = hton(ps[i]->word);
			} else {
				memset(&dest[i], 0, sizeof(bcm2_patch));
			}
		}
	}

	bcm2_write_args get_write_args(uint32_t offset, uint32_t length)
	{
		auto profile = m_intf->profile();
		uint32_t kseg1 = profile->kseg1();
		auto cfg = m_intf->version().codecfg();
		auto funcs = m_intf->version().functions(m_space.name());

		auto fl_write = funcs["write"];
		auto fl_erase = funcs["erase"];

		bcm2_write_args args = { ":%x:%x", "\r\n" };
		args.flags = hton(fl_write.args() | fl_erase.args());
		args.length = hton(length);
		args.chunklen = hton(limits_read().max);
		args.index = 0;
		args.fl_write = 0;

		if (space().is_ram()) {
			args.buffer = hton(offset);
			args.offset = 0;
		} else {
			args.buffer = hton(kseg1 | cfg["buffer"]);
			args.offset = hton(offset);
		}

		args.printf = hton(cfg["printf"]);

		if (cfg["sscanf"] && cfg["getline"]) {
			args.xscanf = hton(cfg["sscanf"]);
			args.getline = hton(cfg["getline"]);
		} else if (cfg["scanf"]) {
			args.xscanf = hton(cfg["scanf"]);
		}

		if (fl_erase.addr()) {
			args.fl_erase = hton(kseg1 | fl_erase.addr());
			copy_patches(args.erase_patches, fl_erase, kseg1);
		}

		if (fl_write.addr()) {
			args.fl_write = hton(kseg1 | fl_write.addr());
			copy_patches(args.write_patches, fl_write, kseg1);
		}

		if (!args.printf || !args.xscanf || (!space().is_mem() && !args.fl_write)) {
			throw user_error("profile " + profile->name() + " does not support fast write mode; use -s flag");
		}

		return args;
	}

	bcm2_read_args get_read_args(uint32_t offset, uint32_t length)
	{
		auto profile = m_intf->profile();
		uint32_t kseg1 = profile->kseg1();
		auto cfg = m_intf->version().codecfg();
		auto funcs = m_intf->version().functions(m_space.name());

		auto fl_read = funcs["read"];

		if (!cfg["printf"] || (!m_space.is_mem() && (!cfg["buffer"] || !fl_read.addr()))) {
			throw user_error("profile " + profile->name() + " does not support fast dump mode; use -s flag");
		}

		bcm2_read_args args = { ":%x", "\r\n" };
		args.offset = hton(offset);
		args.length = hton(length);
		args.chunklen = hton(limits_read().max);
		args.printf = hton(kseg1 | cfg["printf"]);

		if (m_space.is_mem()) {
			args.buffer = 0;
			args.fl_read = 0;
		} else {
			args.buffer = hton(kseg1 | cfg["buffer"]);
			args.flags = hton(fl_read.args());
			args.fl_read = hton(kseg1 | fl_read.addr());
		}

		copy_patches(args.patches, fl_read, kseg1);

		return args;
	}

	uint32_t m_loadaddr = 0;
	uint32_t m_entry = 0;

	bool m_write = false;
	uint32_t m_rw_offset = 0;
	uint32_t m_rw_length = 0;

	rwx::sp m_ram;
};

template<class T> rwx::sp create_rwx(const interface::sp& intf, const addrspace& space)
{
	auto ret = make_shared<T>();
	ret->set_interface(intf);
	ret->set_addrspace(space);
	return ret;
}

rwx::sp create_code_rwx(const interface::sp& intf, const addrspace& space)
{
	try {
		rwx::sp ret = make_shared<code_rwx>();
		ret->set_interface(intf);
		ret->set_addrspace(space);
		return ret;
	} catch (const exception& e) {
		logger::d() << e.what() << endl;
		logger::i() << "falling back to safe method" << endl;
		return rwx::create(intf, space.name(), true);
	}
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
};

void bfc_cmcfg::do_read_chunk(uint32_t offset, uint32_t length)
{
	m_intf->writeln("/docsis_ctl/cfg_hex_show");
}

bool bfc_cmcfg::is_ignorable_line(const string& line)
{
	//bool ret = line.size() != 75 || line.substr(55, 4) != "  | ";
	bool ret = line.size() < 58 || line.size() > 73 || line.substr(53, 4) != "  | ";
	return ret;
}

string bfc_cmcfg::parse_chunk_line(const string& line, uint32_t)
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
sigh_type rwx::s_sighandler_orig = nullptr;
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

	throw runtime_error("rwx does not support " + name + ((cap & cap_special) ? " special" : "") + " capability");
}

void rwx::exec(uint32_t offset)
{
	require_capability(cap_exec);
	if (!exec_impl(offset)) {
		throw runtime_error("failed to execute function at offset 0x" + to_hex(offset));
	}
}

void rwx::dump(uint32_t offset, uint32_t length, std::ostream& os, bool resume)
{
	require_capability(cap_read);

	auto ioex = scoped_ios_exceptions::failbad(os);
	auto cleaner = make_cleaner();

	if (capabilities() & cap_special) {
		if (resume) {
			throw invalid_argument("resume not supported with special reader");
		}

		do_init(0, 0, false);
		update_progress(0, 0, false, true);
		read_special(offset, length, os);
		end_progress(false);
		return;
	} else {
		m_space.check_range(offset, length);
	}

	if (resume) {
		uint32_t completed = get_stream_size(os);
		if (completed >= length) {
			logger::i() << "nothing to resume" << endl;
			return;
		} else {
			uint32_t overlap = limits_read().max * 2;
			completed = align_left(completed, overlap);
			if (completed >= overlap) {
				completed -= overlap;
				offset += completed;
				length -= completed;
				logger::v() << "resuming at offset 0x" + to_hex(offset) << endl;
			}
		}
	}

	uint32_t offset_r = align_left(offset, limits_read().alignment);
	uint32_t length_r = align_right(length + (offset - offset_r), limits_read().min);
	uint32_t length_w = length;

	if (offset_r != offset || length_r != length) {
		logger::d() << "adjusting dump params: 0x" << to_hex(offset) << "," << length
				<< " -> 0x" << to_hex(offset_r) << "," << length_r << endl;
	}

	do_init(offset_r, length_r, false);
	init_progress(offset_r, length_r, false);

	bool show_hdr = true;
	string hdrbuf;

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
			chunk_w = chunk.substr(pos, min(n - pos, length_w));
		} else if (offset_r >= offset && length_w) {
			chunk_w = chunk.substr(0, min(n, length_w));
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

		length_w -= chunk_w.size();
		length_r -= n;
		offset_r += n;
	}
}

void rwx::dump(const string& spec, ostream& os, bool resume)
{
	require_capability(cap_read);
	uint32_t offset, length;
	parse_offset_size(*this, spec, offset, length, false);
	return dump(offset, length, os, resume);
}

string rwx::read(uint32_t offset, uint32_t length)
{
	ostringstream ostr;
	dump(offset, length, ostr);
	return ostr.str();
}

void rwx::write(const string& spec, istream& is)
{
	require_capability(cap_write);
	uint32_t offset, length;
	parse_offset_size(*this, spec, offset, length, true);
	write(offset, is, length);
}

void rwx::write(uint32_t offset, istream& is, uint32_t length)
{
	require_capability(cap_write);

	if (!length) {
		length = get_stream_size(is);
	}

	auto ioex = scoped_ios_exceptions::failbad(is);

	string buf;
	buf.resize(length);
	if (is.readsome(&buf[0], length) != length) {
		throw runtime_error("failed to read " + to_string(length) + " bytes");
	}

	write(offset, buf);
}

void rwx::write(uint32_t offset, const string& buf, uint32_t length)
{
	require_capability(cap_write);

	if (!length) {
		length = buf.size();
	}

	m_space.check_range(offset, length);

	limits lim = limits_write();

	uint32_t offset_w = align_left(offset, lim.min);
	uint32_t length_w = align_right(length + (offset - offset_w), lim.min);

	if (offset_w != offset || length_w != length) {
		logger::d() << "adjusting write params: 0x" << to_hex(offset) << "," << length
				<< " -> 0x" << to_hex(offset_w) << "," << length_w << endl;
		throw user_error("non-aligned writes are not yet supported; alignment is " + to_string(lim.min));
	}

	string contents;
	if (false && (capabilities() & cap_read)) {
		contents = read(offset_w, length_w);
	}

	auto cleaner = make_cleaner();
	do_init(offset_w, length_w, true);
	init_progress(offset_w, length_w, true);

	string buf_w;

	if (offset_w != offset) {
		buf_w += read(offset_w, offset - offset_w);
	}

	buf_w += buf;

	if (length_w != length) {
		buf_w += read(offset + buf.size(), length_w - length);
	}

	throw_if_interrupted();

	unsigned retries = 0;

	while (length_w) {
		uint32_t n;
		if (lim.max == lim.alignment) {
			n = length_w < lim.max ? lim.min : lim.max;
		} else {
			n = min(length_w, lim.max);
		}
		auto begin = buf_w.size() - length_w;
		//string chunk(buf_w.substr(buf_w.size() - length_w, n));
		string chunk(buf_w.substr(begin, n));

		if (contents.empty() || contents.substr(begin, n) != chunk) {
			bool ok = false;

			while (!ok) {
				string what;
				try {
					ok = write_chunk(offset_w, chunk);
				} catch (const exception& e) {
					what = e.what();
				}

				throw_if_interrupted();

				if (!ok) {
					string msg = "failed to write chunk 0x" + to_hex(offset_w);
					if (!what.empty()) {
						msg += " (" + what + ")";
					}

					if (++retries < 5 /*&& wait_for_interface(m_intf)*/) {
						logger::d() << endl << msg << "; retrying" << endl;
						//on_chunk_retry(offset_w, chunk.size());
						continue;
					}

					 throw runtime_error(msg);
				} else {
					retries = 0;
				}
			}
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

	update_progress(offset_w, length_w);
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
				return create_code_rwx(intf, space);
			}
		} else if (!safe) {
			return create_code_rwx(intf, space);
		}
	} else if (intf->name() == "bfc") {
		if (space.is_mem()) {
			return create_rwx<bfc_ram>(intf, space);
		} else if (!safe && bfc_flash2::is_supported(intf, space.name())) {
			return create_rwx<bfc_flash2>(intf, space);
		} else {
			return create_rwx<bfc_flash>(intf, space);
		}
	}

	throw invalid_argument("no such rwx: " + intf->name() + "," + type + ((safe ? "," : ",un") + string("safe")));
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


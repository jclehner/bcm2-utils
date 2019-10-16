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
#include <fstream>
#include "progress.h"
#include "mipsasm.h"
#include "util.h"
#include "rwx.h"
#include "ps.h"

#define BFC_FLASH_READ_DIRECT

using namespace std;

namespace bcm2dump {
namespace {

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

	string line, linebuf, chunk, last;
	uint32_t pos = offset;
	mstimer t;
	unsigned timeout = chunk_timeout(offset, length);

	do {
		while ((!length || chunk.size() < length) && m_intf->pending()) {
			throw_if_interrupted();

			line = m_intf->readln();
			if (line.empty()) {
				break;
			}

			line = trim(line);

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
					if (retries >= max_retry_count) {
						throw runtime_error(msg);
					}

					logger::d() << endl << msg << endl;
					break;
				}
			}
		}
	} while (timeout && t.elapsed() < timeout);

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
	return m_intf->runcmd("/call func -a 0x" + to_hex(offset), "Calling function 0x");
};

bool bfc_ram::write_chunk(uint32_t offset, const string& chunk)
{
	if (m_intf->is_privileged()) {
		uint32_t val = chunk.size() == 4 ? ntoh(extract<uint32_t>(chunk)) : chunk[0];
		return m_intf->runcmd("/write_memory -s " + to_string(chunk.size()) + " 0x" +
				to_hex(offset, 0) + " 0x" + to_hex(val, 0), "Writing");
	} else {
		// diag writemem only supports writing bytes
		return m_intf->runcmd("/system/diag writemem 0x" + to_hex(offset, 0) + " 0x" +
				to_hex(chunk[0] & 0xff, 0), "Writing");
	}
}

void bfc_ram::do_read_chunk(uint32_t offset, uint32_t length)
{
	if (m_intf->is_privileged()) {
		m_intf->runcmd("/read_memory -s 4 -n " + to_string(length) + " 0x" + to_hex(offset));
	} else {
		m_intf->runcmd("/system/diag readmem -s 4 -n " + to_string(length) + " 0x" + to_hex(offset));
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
	string linebuf;

	if (!m_hint_decimal) {
		if (offset != hex_cast<uint32_t>(line.substr(0, 8))) {
			throw runtime_error("offset mismatch");
		}
		for (unsigned i = 0; i < 4; ++i) {
			linebuf += to_buf(hton(hex_cast<uint32_t>(line.substr((i + 1) * 10, 8))));
		}
	} else {
		auto beg = line.find(": ");
		if (offset != lexical_cast<uint32_t>(line.substr(0, beg))) {
			throw runtime_error("offset mismatch");
		}

		for (unsigned i = 0; i < 4; ++i) {
			beg = line.find_first_of("0123456789", beg);
			auto end = line.find_first_not_of("0123456789", beg);
			linebuf += to_buf(hton(lexical_cast<uint32_t>(line.substr(beg, end - beg))));
			beg = end;
		}
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
		m_intf->runcmd(cmd);

#if 0
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
			call(cmd, "read", 60);
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
		m_intf->runcmd("/flash/open " + m_partition.altname());

		bool opened = false;
		bool retry = false;

		m_intf->foreach_line([&opened, &retry] (const string& line) {
			if (contains(line, "opened twice")) {
				retry = true;
				opened = false;
			} else if (contains(line, "driver opened")) {
				opened = true;
			}

			return false;
		}, 10000, 10000);

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
	uint32_t val = chunk.size() == 4 ? ntoh(extract<uint32_t>(chunk)) : chunk[0];
	return m_intf->runcmd("/flash/write " + to_string(chunk.size()) + " 0x"
			+ to_hex(offset) + " 0x" + to_hex(val), "successfully written");
}

void bfc_flash::do_read_chunk(uint32_t offset, uint32_t length)
{
	offset = to_partition_offset(offset);
	if (use_direct_read()) {
		m_intf->runcmd("/flash/readDirect " + to_string(length) + " " + to_string(offset));
	} else {
		m_intf->runcmd("/flash/read 4 " + to_string(length) + " " + to_string(offset));
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
	string linebuf;

	if (use_direct_read()) {
		for (unsigned i = 0; i < 16; ++i) {
			// don't change this to uint8_t
			uint32_t val = hex_cast<uint32_t>(line.substr(i * 3 + (i / 4) * 2, 2));
			if (val > 0xff) {
				throw runtime_error("value out of range: 0x" + to_hex(val));
			}

			linebuf += char(val);
		}
	} else {
		for (size_t i = 0; i < line.size(); i += 9) {
			linebuf += to_buf(hton(hex_cast<uint32_t>(line.substr(i, 8))));

			if (!(i % 128)) {
				update_progress(offset + i, 0);
			}
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
		m_intf->runcmd("r");
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
		if (!m_intf->runcmd("w", "Write memory.", true)) {
			return false;
		}

		m_intf->writeln(to_hex(offset, 0));
		uint32_t val = ntoh(extract<uint32_t>(chunk));
		return m_intf->runcmd(to_hex(val) + "\r\n", "Main Menu");
	} catch (const exception& e) {
		// ensure that we're in a sane state
		m_intf->runcmd("\r\n", "Main Menu");
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
			throw runtime_error("offset mismatch");
		}

		return to_buf(hton(hex_cast<uint32_t>(line.substr(19, 8))));
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
#include "rwcode.c"

class code_rwx : public parsing_rwx
{
	public:
	code_rwx() {}

	virtual limits limits_read() const override
	{ return limits(4, 16, 0x4000); }

	virtual limits limits_write() const override
	{ return limits(16, 16, 0x4000); }

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
		} else if (cfg["rwcode"] & 0xffff) {
			throw runtime_error("rwcode address must be aligned to 64k");
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
				linebuf += to_buf(hton(hex_cast<uint32_t>(val)));
			}
		}

		return linebuf;
	}

	protected:
	virtual bool write_chunk(uint32_t offset, const string& chunk) override
	{
		m_ram->exec(m_loadaddr + m_entry);
		for (size_t i = 0; i < chunk.size(); i += 16) {
			string line;
			for (size_t k = 0; k < 4; ++k) {
				line += ":" + to_hex(chunk.substr(i + k * 4, 4));
			}
			m_intf->writeln(line);

			line = trim(m_intf->readln());
			if (line.empty() || line[0] != ':' || hex_cast<uint32_t>(line.substr(1)) != (offset + i)) {
				throw runtime_error("expected offset, got '" + line + "'");
			}

			update_progress(offset + i, 16);
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

		uint32_t remaining = m_rw_length - (offset - m_rw_offset);

		if (!m_write) {
			patch32(m_code, 0x10, offset);
			m_ram->write(m_loadaddr + 0x10, m_code.substr(0x10, 4));

			patch32(m_code, 0x1c, remaining);
			m_ram->write(m_loadaddr + 0x1c, m_code.substr(0x1c, 4));
		} else {
			// TODO: implement if we ever use on_chunk_retry for writes
		}
	}

	unsigned chunk_timeout(uint32_t offset, uint32_t length) const override
	{
		if (offset != m_rw_offset || !m_read_func.addr()) {
			return parsing_rwx::chunk_timeout(offset, length);
		} else {
			return 60 * 1000;
		}
	}

	void init(uint32_t offset, uint32_t length, bool write) override
	{
		const profile::sp& profile = m_intf->profile();
		auto cfg = m_intf->version().codecfg();
		auto funcs = m_intf->version().functions(m_space.name());

		if (cfg["buflen"] && length > cfg["buflen"]) {
			throw user_error("requested length exceeds buffer size ("
					+ to_string(cfg["buflen"]) + " b)");
		}

		if (write && !m_space.is_ram()) {
			throw user_error("writing to non-ram address space is not supported");
		}

		m_write = write;
		m_rw_offset = offset;
		m_rw_length = length;

		uint32_t kseg1 = profile->kseg1();
		m_loadaddr = kseg1 | (cfg["rwcode"] + (write ? 0 : 0 /*0x10000*/));

		// TODO: check whether we have a custom code file
		if (true) {
			if (!write) {
				m_read_func = funcs["read"];

				m_code = string(reinterpret_cast<const char*>(dumpcode), sizeof(dumpcode));
				m_entry = 0x4c;

				if (!cfg["printf"] || (!m_space.is_mem() && (!cfg["buffer"] || !m_read_func.addr()))) {
					throw user_error("profile " + profile->name() + " does not support fast dump mode; use -s flag");
				}

				patch32(m_code, 0x10, 0);
				patch32(m_code, 0x14, kseg1 | cfg["buffer"]);
				patch32(m_code, 0x18, offset);
				patch32(m_code, 0x1c, length);
				patch32(m_code, 0x20, limits_read().max);
				patch32(m_code, 0x24, kseg1 | cfg["printf"]);

				if (m_read_func.addr()) {
					patch32(m_code, 0x0c, m_read_func.args());
					patch32(m_code, 0x28, kseg1 | m_read_func.addr());

					unsigned i = 0;
					for (auto patch : m_read_func.patches()) {
						uint32_t off = 0x2c + (8 * i++);
						uint32_t addr = patch->addr;
						patch32(m_code, off, addr ? (kseg1 | addr) : 0);
						patch32(m_code, off + 4, addr ? patch->word : 0);
					}
				}
			} else {
				m_write_func = funcs["write"];
				m_erase_func = funcs["erase"];

				m_code = string(reinterpret_cast<const char*>(writecode), sizeof(writecode));
				m_entry = WRITECODE_ENTRY;

				patch32(m_code, WRITECODE_CFGOFF + 0x00, m_write_func.args() | m_erase_func.args());
				patch32(m_code, WRITECODE_CFGOFF + 0x04, m_space.is_ram() ? offset : (kseg1 | cfg["buffer"]));
				patch32(m_code, WRITECODE_CFGOFF + 0x08, 0);
				patch32(m_code, WRITECODE_CFGOFF + 0x0c, limits_write().max);

				if (!m_space.is_ram()) {
					patch32(m_code, WRITECODE_CFGOFF + 0x10, offset);
					try {
						patch32(m_code, WRITECODE_CFGOFF + 0x14, m_space.partition(offset).size());
					} catch (const user_error& e) {
						throw user_error("writing to random offsets within a flash partition is not supported");
					}
				} else {
					patch32(m_code, WRITECODE_CFGOFF + 0x10, 0);
					patch32(m_code, WRITECODE_CFGOFF + 0x14, 0);
				}

				patch32(m_code, WRITECODE_CFGOFF + 0x18, kseg1 | cfg["printf"]);

				if (cfg["printf"] && cfg["sscanf"] && cfg["getline"]) {
					patch32(m_code, WRITECODE_CFGOFF + 0x1c, kseg1 | cfg["sscanf"]);
					patch32(m_code, WRITECODE_CFGOFF + 0x20, kseg1 | cfg["getline"]);
				} else if (cfg["printf"] && cfg["scanf"]) {
					patch32(m_code, WRITECODE_CFGOFF + 0x1c, kseg1 | cfg["scanf"]);
					patch32(m_code, WRITECODE_CFGOFF + 0x20, 0);
				} else {
					throw user_error("profile " + profile->name() + " does not support fast write mode; use -s flag");
				}

				patch32(m_code, WRITECODE_CFGOFF + 0x24, m_write_func.addr());
				patch32(m_code, WRITECODE_CFGOFF + 0x28, m_erase_func.addr());

				patch32(m_code, WRITECODE_CFGOFF + 0x2c, length);
			}

			uint32_t codesize = m_code.size();
			if (mipsasm_resolve_labels(reinterpret_cast<uint32_t*>(&m_code[0]), &codesize, m_entry) != 0) {
				throw runtime_error("failed to resolve mips asm labels");
			}

			m_code.resize(codesize);

#if 1
			ofstream("code.bin").write(m_code.data(), m_code.size());
#endif

			uint32_t expected = 0xc0de0000 | crc16_ccitt(m_code.substr(m_entry, m_code.size() - 4 - m_entry));
			uint32_t actual = ntoh(extract<uint32_t>(m_ram->read(m_loadaddr + m_code.size() - 4, 4)));
			bool quick = (expected == actual);

			patch32(m_code, codesize - 4, expected);

			progress pg;
			progress_init(&pg, m_loadaddr, m_code.size());

			if (m_prog_l && !quick) {
				logger::i("updating code at 0x%08x (%u b)\n", m_loadaddr, codesize);
			}

			for (unsigned pass = 0; pass < 2; ++pass) {
				string ramcode = m_ram->read(m_loadaddr, m_code.size());
				for (uint32_t i = 0; i < m_code.size(); i += 4) {
					if (!quick && pass == 0 && m_prog_l) {
						progress_add(&pg, 4);
						logger::i("\r ");
						progress_print(&pg, stdout);
					}

					if (ramcode.substr(i, 4) != m_code.substr(i, 4)) {
						if (pass == 1) {
							throw runtime_error("dump code verification failed at 0x" + to_hex(i + m_loadaddr, 8));
						}
						m_ram->write(m_loadaddr + i, m_code.substr(i, 4));
					}
				}
			}

			logger::i("\n");
		}
	}

	string m_code;
	uint32_t m_loadaddr = 0;
	uint32_t m_entry = 0;

	bool m_write = false;
	uint32_t m_rw_offset = 0;
	uint32_t m_rw_length = 0;

	func m_read_func;
	func m_write_func;
	func m_erase_func;

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
	m_intf->runcmd("/docsis_ctl/cfg_hex_show");
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

					if (++retries < 5 && wait_for_interface(m_intf)) {
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


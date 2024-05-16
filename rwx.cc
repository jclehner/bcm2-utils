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

#ifdef BCM2DUMP_WITH_SNMP
#include "snmp.h"
#endif

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
	patch<uint32_t>(buf, offset, h_to_be(n));
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

string parse_hex_data(const string& line, bool is_byte_data)
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

		if (is_byte_data) {
			if (n > 0xff) {
				throw bad_chunk_line::regular("invalid byte: 0x" + to_hex(n));
			}

			linebuf += char(n);
		} else {
			linebuf += to_buf(be_to_h(n));
		}
	}

	return linebuf;
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

	bcm2dump::sp<cmdline_interface> interface() const
	{ return dynamic_pointer_cast<cmdline_interface>(m_intf); }
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
	logger::t() << "read_chunk_impl: calling do_read_chunk" << endl;

	do_read_chunk(offset, length);

	uint32_t pos = offset;
	string chunk;

	logger::t() << "read_chunk_impl: consuming lines" << endl;

	interface()->foreach_line_raw([this, &chunk, &pos, &length, &retries] (const string& line) {
		throw_if_interrupted();
		string tline = trim(line);
		if (!is_ignorable_line(tline)) {
			try {
				string linebuf = parse_chunk_line(tline, pos);
				pos += linebuf.size();
				chunk += linebuf;
				update_progress(pos, chunk.size());

				if (linebuf.empty()) {
					logger::t() << "no bytes found in '" << tline << "'" << endl;
				}
			} catch (const bad_chunk_line& e) {
				string msg = "bad chunk line @" + to_hex(pos) + ": '" + tline + "' (" + e.what() + ")";
				if (e.critical() && retries >= max_retry_count) {
					throw runtime_error(msg);
				}

				logger::t() << endl << msg << endl;
			} catch (const exception& e) {
				logger::d() << "error while parsing '" << tline << "': " << e.what() << endl;
				return true;
			}
		}

		return !(chunk.size() < length);
	}, 10000);

	logger::t() << "read_chunk_impl: done reading lines" << endl;

	// consume any more output
	interface()->wait_quiet(20);

	if (length && (chunk.size() != length)) {
		string msg = "read incomplete chunk 0x" + to_hex(offset)
					+ ": " + to_string(chunk.size()) + "/" +to_string(length);
		if (retries < max_retry_count) {
			// if the dump is still underway, we need to wait for it to finish
			// before issuing the next command. wait for up to 10 seconds.

			if (interface()->wait_ready()) {
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
	{ return limits(4, 16, 2 * 8192); }

	virtual limits limits_write() const override
	{
		// diag writemem only supports writing bytes
		if (m_diag_cmd.empty()) {
			return limits(4, 1, 4);
		} else {
			return limits(1, 1, 1);
		}
	}

	virtual void set_interface(const interface::sp& intf) override;

	unsigned capabilities() const override
	{ return m_space.is_ram() ? m_ram_caps : (cap_read | (m_space.is_writable() ? cap_write : 0)); }

	protected:
	virtual bool exec_impl(uint32_t offset) override;
	virtual bool write_chunk(uint32_t offset, const string& chunk) override;
	virtual bool is_ignorable_line(const string& line) override;
	virtual void do_read_chunk(uint32_t offset, uint32_t length) override;
	virtual string parse_chunk_line(const string& line, uint32_t offset) override;

	private:
	string m_diag_cmd;
	unsigned m_ram_caps = cap_rwx;

};

void bfc_ram::set_interface(const interface::sp& intf)
{
	parsing_rwx::set_interface(intf);
	m_diag_cmd = "";
	m_ram_caps = 0;

	auto lines = interface()->run_raw("/find_command call", 200);
	if (find(lines.begin(), lines.end(), "/call") != lines.end()) {
		m_ram_caps = cap_exec;
	} else {
		logger::d() << "no /call command found" << endl;
	}

	// TODO actually check for write capabilities too

	lines = interface()->run_raw("/find_command read_memory", 200);
	if (find(lines.begin(), lines.end(), "/read_memory") != lines.end()) {
		// we have a system-wide /read_memory command
		m_ram_caps |= (cap_read | cap_write);
		return;
	}

	bool strip_console_prefix = false;
	interface()->run("cd /");

	lines = interface()->run_raw("/find_command readmem", 200);
	auto it = find_if(lines.begin(), lines.end(), [&strip_console_prefix](auto l) {
		// Some devices show a CM/Console> prompt after telnet login,
		// even after a `cd /` command. In that case, `/find_command readmem`
		// might return `/Console/system/diag`, even though it must be called
		// as `/system/diag`.
		if (l.find("CM/Console>") != string::npos) {
			strip_console_prefix = true;
		}

		return ends_with(l, "/diag readmem");
	});

	if (it != lines.end()) {
		m_diag_cmd = it->substr(0, it->size() - strlen(" readmem"));
		if (strip_console_prefix && starts_with(m_diag_cmd, "/Console/")) {
			// Keep the leading slash!
			m_diag_cmd = m_diag_cmd.substr(strlen("/Console/"));
		}
		logger::d() << "using " << m_diag_cmd << " command for memory access" << endl;
		m_ram_caps |= (cap_read | cap_write);
		return;
	}
}

bool bfc_ram::exec_impl(uint32_t offset)
{
	return interface()->run("/call func -a 0x" + to_hex(offset), "Calling function 0x");
};

bool bfc_ram::write_chunk(uint32_t offset, const string& chunk)
{
	if (m_diag_cmd.empty()) {
		uint32_t val = chunk.size() == 4 ? be_to_h(extract<uint32_t>(chunk)) : chunk[0];
		return interface()->run("/write_memory -s " + to_string(chunk.size()) + " 0x" +
				to_hex(offset, 0) + " 0x" + to_hex(val, 0), "Writing");
	} else {
		return interface()->run(m_diag_cmd + " writemem 0x" + to_hex(offset, 0) + " 0x" +
				to_hex(chunk[0] & 0xff, 0), "Writing");
	}
}

void bfc_ram::do_read_chunk(uint32_t offset, uint32_t length)
{
	if (m_diag_cmd.empty()) {
		interface()->writeln("/read_memory -s 4 -n " + to_string(length) + " 0x" + to_hex(offset));
	} else {
		interface()->writeln(m_diag_cmd + " readmem -s 4 -n " + to_string(length) + " 0x" + to_hex(offset));
	}
}

bool bfc_ram::is_ignorable_line(const string& line)
{
	if (line.size() >= 50) {
		if (line.substr(8, 2) == ": " && line.substr(48, 2) == " |") {
			return false;
		} else if (contains(line, ": ") && (contains(line, " | ") || ends_with(line, " |"))) {
			// if another message is printed by the firmware, the dump
			// output sometimes switches to an all-decimal format.
			return false;
		}
	}

	return true;
}

string bfc_ram::parse_chunk_line(const string& line, uint32_t offset)
{
	const char* fmts[] = {
		"%x: %x  %x  %x  %x",
		"%u: %u  %u  %u  %u",
	};

	uint32_t data[4];
	uint32_t off;
	int n;

	for (const char* fmt : fmts) {
		n = sscanf(line.c_str(), fmt, &off, &data[0],
				&data[1], &data[2], &data[3]);

		if (n > 1 && off == offset) {
			break;
		}
	}

	if (!n) {
		throw bad_chunk_line::regular();
	} else if (off != offset) {
		throw bad_chunk_line::critical("offset mismatch");
	}

	string linebuf;

	for (int i = 0; i < (n - 1); ++i) {
		linebuf += to_buf(be_to_h(data[i]));
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

		auto ver = interface()->version();
		m_cfg = ver.codecfg();
		uint32_t buflen = m_cfg["buflen"];

		if (buflen && length > buflen) {
			throw user_error("requested length exceeds buffer size ("
					+ to_string(buflen) + " b)");
		}

		m_dump_offset = offset;
		m_dump_length = length;
		m_funcs = ver.functions(m_space.name());

		call_open_close("open", offset, length);

		patch(m_funcs["read"]);
	}

	virtual void cleanup() override
	{
		bfc_ram::cleanup();
		call_open_close("close", m_dump_offset, m_dump_length);
	}

	virtual unsigned chunk_timeout(uint32_t offset, uint32_t length) const override
	{
		return 5 * 1000;
	}

	virtual string parse_chunk_line(const string& line, uint32_t offset) override
	{
		return bfc_ram::parse_chunk_line(line, m_cfg["buffer"] + (offset % limits_read().max));
	}

	virtual void do_read_chunk(uint32_t offset, uint32_t length) override
	{
		call_read(offset, length);
		bfc_ram::do_read_chunk(m_cfg["buffer"], length);
	}

	private:
	void patch(const func& f)
	{
		for (auto p : f.patches()) {
			if (p->addr && !write_chunk(p->addr | interface()->profile()->kseg1(), to_buf(h_to_be(p->word)))) {
				throw runtime_error("failed to patch word at 0x" + to_hex(p->addr));
			}
		}
	}

	void call(const string& cmd, const string& name, unsigned timeout = 100)
	{
		interface()->run(cmd, timeout);
	}

	void call_read(uint32_t offset, uint32_t length)
	{
		func read = m_funcs["read"];
		if (read.addr()) {
			string cmd = mkcmd(read);
			if (read.args() == BCM2_READ_FUNC_BOL) {
				add_args(cmd, { m_cfg["buffer"], offset, length });
			} else if (read.args() == BCM2_READ_FUNC_OBL) {
				add_args(cmd, { offset, m_cfg["buffer"], length });
			} else {
				throw runtime_error("unsupported 'read' args");
			}

			//patch(read);
			call(cmd, "read");
		}
	}

	void call_open_close(const string& name, uint32_t offset, uint32_t length)
	{
		func f = m_funcs[name];
		if (f.addr()) {
			string cmd = mkcmd(f, { offset });

			if (f.args() == BCM2_ARGS_OL) {
				add_arg(cmd, length);
			} else if (f.args() == BCM2_ARGS_OE) {
				add_arg(cmd, offset + length);
			} else {
				throw runtime_error("unsupported '" + name + "' args");
			}

			//patch(f);
			call(cmd, name);
		}
	}

	string mkcmd(const func& f, vector<uint32_t> a = {})
	{
		string ret = "/call func -a 0x" + to_hex(f.addr() | interface()->profile()->kseg1());
		add_args(ret, a);
		return ret;
	}

	static void add_arg(string& cmd, uint32_t arg)
	{ cmd += " 0x" + to_hex(arg); }

	static void add_args(string& cmd, vector<uint32_t> args)
	{
		for (auto a : args) {
			add_arg(cmd, a);
		}
	}

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

	virtual limits limits_read() const override;

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

rwx::limits bfc_flash::limits_read() const
{
	auto v = interface()->version();
	auto readsize = v.get_opt_num("bfc:flash_readsize", 0);
	if (readsize) {
		return { readsize, readsize, readsize };
	} else {
		return { 1, 16, 4096 };
	}
}

void bfc_flash::init(uint32_t, uint32_t, bool write)
{
	if (m_partition.name().empty()) {
		throw user_error("partition name required");
	}

	for (unsigned pass = 0; pass < 2; ++pass) {
		auto lines = interface()->run("/flash/open " + m_partition.altname(), 5000);

		bool opened = false;

		for (auto line : lines) {
			if (contains(line, "opened twice")) {
				opened = false;
			} else if (contains(line, "driver opened")) {
				opened = true;
			}
		}

		if (opened) {
			break;
		} else if (pass == 0) {
			logger::d() << "reinitializing flash driver" << endl;
			cleanup();
			interface()->run("/flash/deinit", "Deinitializing");
			interface()->run("/flash/init", 5000);
		} else {
			throw runtime_error("failed to open partition " + m_partition.name());
		}
	}
}

void bfc_flash::cleanup()
{
	interface()->run("/flash/close", "driver closed");
}

bool bfc_flash::write_chunk(uint32_t offset, const std::string& chunk)
{
	offset = to_partition_offset(offset);
	uint32_t val = chunk.size() == 4 ? be_to_h(extract<uint32_t>(chunk)) : chunk[0];
	return interface()->run("/flash/write " + to_string(chunk.size()) + " 0x"
			+ to_hex(offset) + " 0x" + to_hex(val), "successfully written");
}

void bfc_flash::do_read_chunk(uint32_t offset, uint32_t length)
{
	offset = to_partition_offset(offset);
	if (use_direct_read()) {
		interface()->writeln("/flash/readDirect " + to_string(length) + " " + to_string(offset));
	} else {
		interface()->writeln("/flash/read 4 " + to_string(length) + " " + to_string(offset));
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
	return parse_hex_data(line, use_direct_read());
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
	auto v = interface()->version();

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
	auto v = interface()->version();

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

	private:
	bool m_write = false;
};

void bootloader_ram::init(uint32_t offset, uint32_t length, bool write)
{
	if (!write) {
		interface()->write("r");
	} else {
		interface()->writeln();
	}

	m_write = write;
}

void bootloader_ram::cleanup()
{
	if (!m_write) {
		interface()->run("\r");
	} else {
		interface()->writeln();
	}
}

bool bootloader_ram::write_chunk(uint32_t offset, const string& chunk)
{
	try {
		if (!interface()->run("w", "Write memory.", true)) {
			return false;
		}

		interface()->writeln(to_hex(offset, 0));
		uint32_t val = be_to_h(extract<uint32_t>(chunk));
		interface()->writeln(to_hex(val));
		return true;
	} catch (const exception& e) {
		// ensure that we're in a sane state
		interface()->run("\r");
		return false;
	}
}

void bootloader_ram::do_read_chunk(uint32_t offset, uint32_t length)
{
	interface()->writeln("0x" + to_hex(offset, 0));
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

		return to_buf(h_to_be(hex_cast<uint32_t>(line.substr(19, 8))));
	}

	throw bad_chunk_line::regular();
}

bool bootloader_ram::exec_impl(uint32_t offset)
{
	interface()->run("");
	if (interface()->run("j", "address (hex):", true)) {
		interface()->writeln(to_hex(offset));
		return true;
	}
	return false;
}

// some Netgear bootloaders have a hidden option `c`, that allows reading from flash directly
class bootloader_flash : public parsing_rwx
{
	public:
	virtual ~bootloader_flash() {}

	virtual limits limits_write() const override
	{ return limits(0, 0, 0); }

	virtual limits limits_read() const override
	{ return limits(4, 4, 0x4000); }

	virtual unsigned capabilities() const override
	{ return cap_read; }

	static bool is_supported(const interface::sp& intf, const addrspace& space)
	{
		auto i = dynamic_pointer_cast<cmdline_interface>(intf);

		if (i && enter_flash_read_mode(i)) {
			// exit mode
			i->writeln("");
			return true;
		}

		return false;
	}

	protected:
	virtual void do_read_chunk(uint32_t offset, uint32_t length) override
	{
		if (enter_flash_read_mode(interface())) {
			interface()->writeln(to_hex(offset) + " " + to_string(length));
		} else {
			throw runtime_error("failed to enter flash read mode");
		}
	}

	virtual bool is_ignorable_line(const string& line) override
	{
		return line.size() < 10 || line.substr(8, 2) != "h: ";
	}

	virtual string parse_chunk_line(const string& line, uint32_t offset) override
	{
		if (offset != hex_cast<uint32_t>(line.substr(0, 8))) {
			throw bad_chunk_line::critical("offset mismatch");
		}

		return parse_hex_data(line.substr(10), true);
	}

	private:
	static bool enter_flash_read_mode(const bcm2dump::sp<cmdline_interface>& intf)
	{
		intf->run("");
		return intf->run("c", "read flash. offset(H) count:", true);
	}
};

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

class bolt_ram : public parsing_rwx
{
	public:
	virtual ~bolt_ram() {}

	virtual limits limits_read() const override
	{ return limits(4, 4, 0x8000); }

	virtual limits limits_write() const override
	{ return limits(1, 8, 8); }

	virtual unsigned capabilities() const override
	{ return cap_rwx; }

	protected:
	virtual bool write_chunk(uint32_t offset, const string& chunk) override;
	virtual bool exec_impl(uint32_t offset) override;

	virtual void do_read_chunk(uint32_t offset, uint32_t length) override;
	virtual bool is_ignorable_line(const string& line) override;
	virtual string parse_chunk_line(const string& line, uint32_t offset) override;

	private:
	bool m_write = false;
};

bool bolt_ram::write_chunk(uint32_t offset, const string& chunk)
{
	string flag, data;

	if (chunk.size() == 1) {
		flag = "-b";
		data = to_hex(chunk[0]);
	} else if (chunk.size() == 2) {
		flag = "-h";
		data = to_hex(h_to_le(extract<uint16_t>(chunk)));
	} else if (chunk.size() == 4) {
		flag = "-w";
		data = to_hex(h_to_le(extract<uint32_t>(chunk)));
	} else if (chunk.size() == 8) {
		flag = "-q";
		data = to_hex(h_to_le(extract<uint64_t>(chunk)));
	} else {
		throw invalid_argument("invalid chunk size " + to_string(chunk.size()));
	}

	interface()->run("e " + flag + " 0x" + to_hex(offset) + " 0x" + data);
	return true;
}

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
			linebuf += to_buf(h_to_be(hex_cast<uint32_t>(val)));
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

			interface()->writeln(line);

			line = trim(interface()->readln());
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
			interface()->wait_ready(60);
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
			uint32_t index = offset - m_rw_offset;
			m_ram->write(m_loadaddr + offsetof(bcm2_read_args, index), to_buf(h_to_be(index)));
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
		const profile::sp& profile = interface()->profile();
		auto cfg = interface()->version().codecfg();

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
					code += to_buf(h_to_be(word));
				}
			} else {
				bcm2_write_args args = get_write_args(offset, length);
				m_entry = sizeof(args);
				code = to_buf(args);

				for (uint32_t word : mips_write_code) {
					code += to_buf(h_to_be(word));
				}
			}

			size_t codesize = code.size() - m_entry;

			uint32_t expected = 0xc0de0000 | crc16_ccitt(code.substr(m_entry, codesize));
			uint32_t actual = be_to_h(extract<uint32_t>(m_ram->read(m_loadaddr + codesize, 4)));
			bool quick = (expected == actual);

			code += to_buf(h_to_be(expected));

#if 0
			ofstream("code.bin").write(code.data(), code.size());
#endif

			progress pg;
			progress_init(&pg, m_loadaddr, code.size());

			if (m_prog_l && !quick) {
				logger::i("updating code at 0x%08x (%u b)\n", m_loadaddr, static_cast<unsigned>(code.size()));
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
				dest[i].addr = h_to_be(kseg1 | ps[i]->addr);
				dest[i].word = h_to_be(ps[i]->word);
			} else {
				memset(&dest[i], 0, sizeof(bcm2_patch));
			}
		}
	}

	bcm2_write_args get_write_args(uint32_t offset, uint32_t length)
	{
		auto profile = interface()->profile();
		uint32_t kseg1 = profile->kseg1();
		auto cfg = interface()->version().codecfg();
		auto funcs = interface()->version().functions(m_space.name());

		auto fl_write = funcs["write"];
		auto fl_erase = funcs["erase"];

		bcm2_write_args args = { ":%x:%x", "\r\n" };
		args.flags = h_to_be(fl_write.args() | fl_erase.args());
		args.length = h_to_be(length);
		args.chunklen = h_to_be(limits_read().max);
		args.index = 0;
		args.fl_write = 0;

		if (space().is_ram()) {
			args.buffer = h_to_be(offset);
			args.offset = 0;
		} else {
			args.buffer = h_to_be(kseg1 | cfg["buffer"]);
			args.offset = h_to_be(offset);
		}

		args.printf = h_to_be(cfg["printf"]);

		if (cfg["sscanf"] && cfg["getline"]) {
			args.xscanf = h_to_be(cfg["sscanf"]);
			args.getline = h_to_be(cfg["getline"]);
		} else if (cfg["scanf"]) {
			args.xscanf = h_to_be(cfg["scanf"]);
		}

		if (fl_erase.addr()) {
			args.fl_erase = h_to_be(kseg1 | fl_erase.addr());
			copy_patches(args.erase_patches, fl_erase, kseg1);
		}

		if (fl_write.addr()) {
			args.fl_write = h_to_be(kseg1 | fl_write.addr());
			copy_patches(args.write_patches, fl_write, kseg1);
		}

		if (!args.printf || !args.xscanf || (!space().is_mem() && !args.fl_write)) {
			throw user_error("profile " + profile->name() + " does not support fast write mode; use -s flag");
		}

		return args;
	}

	bcm2_read_args get_read_args(uint32_t offset, uint32_t length)
	{
		auto profile = interface()->profile();
		uint32_t kseg1 = profile->kseg1();
		auto cfg = interface()->version().codecfg();
		auto funcs = interface()->version().functions(m_space.name());

		auto fl_read = funcs["read"];

		if (!cfg["printf"] || (!m_space.is_mem() && (!cfg["buffer"] || !fl_read.addr()))) {
			throw user_error("profile " + profile->name() + " does not support fast dump mode; use -s flag");
		}

		bcm2_read_args args = { ":%x", "\r\n" };
		args.length = h_to_be(length);
		args.index = 0;
		args.chunklen = h_to_be(limits_read().max);
		args.printf = h_to_be(kseg1 | cfg["printf"]);

		if (m_space.is_mem()) {
			args.buffer = h_to_be(offset);
			args.offset = 0;
			args.fl_read = 0;
		} else {
			args.offset = h_to_be(offset);
			args.buffer = h_to_be(kseg1 | cfg["buffer"]);
			args.flags = h_to_be(fl_read.args());
			args.fl_read = h_to_be(kseg1 | fl_read.addr());
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
	interface()->writeln("/docsis_ctl/cfg_hex_show");
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

class bfc_bootassist : public rwx
{
	public:
	virtual limits limits_read() const override
	{ return { 4, 4, 0x10000 }; }

	virtual limits limits_write() const override
	{ return { 0, 0, 0 }; }

	virtual unsigned capabilities() const override
	{ return cap_read; }

	virtual void set_interface(const interface::sp& intf) override
	{
		rwx::set_interface(intf);
		m_ram = rwx::create(intf, "ram");

		auto v = intf->version();
		m_cpuc_reg_request = v.get_opt_num("bootassist:cpuc_reg_request", 0xd3800044);
		m_mbox_reg_cmstate = v.get_opt_num("bootassist:mbox_reg_cmstate", 0xd3800084);
		m_mbox_reg_imgreq = v.get_opt_num("bootassist:mbox_reg_imgreq", 0xd3800090);
		m_mbox_reg_imgbuf = v.get_opt_num("bootassist:mbox_reg_imgbuf", 0xd3800094);
		m_cmstate_value = v.get_opt_num("bootassist:cmstate_value", 7);
		m_request_value = v.get_opt_num("bootassist:request_value", 0x20);
	}

	static bool is_supported(const interface::sp& intf, const addrspace& space)
	{
		if (intf->profile()->arch() != BCM2_3390) {
			return false;
		}

		if (space.name() != "flash1") {
			return false;
		}

		return true;
	}

	protected:
	virtual void init(uint32_t offset, uint32_t length, bool write) override
	{
		auto part = space().partition(offset);

		if (sscanf(part.name().c_str(), "cmrun%u", &m_image) != 1) {
			throw invalid_argument("only cmrun partitions are supported");
		}

		m_cmstate_saved = m_ram->read32(m_mbox_reg_cmstate);
		m_ram->write32(m_mbox_reg_cmstate, m_cmstate_value);
	}

	virtual void cleanup() override
	{
		m_ram->write32(m_mbox_reg_cmstate, m_cmstate_saved);
	}

	virtual std::string read_chunk(uint32_t offset, uint32_t length) override
	{
		unsigned block = (offset / limits_read().max) + 1;

		m_ram->write32(m_cpuc_reg_request, m_request_value);
		m_ram->write32(m_mbox_reg_imgreq, block | (((m_image & 0xffff) - 1) << 31));

		mstimer t;

		do {
			if (m_ram->read32(m_cpuc_reg_request) & m_request_value) {
				auto buffer = m_ram->read32(m_mbox_reg_imgbuf);
				if (buffer == 0xffffffff) {
					throw runtime_error("error retrieving block " + to_string(block));
				}

#if 1
				string chunk;

				const unsigned n = 32;
				auto remaining = length;

				for (uint32_t i = 0; i < length; i += n) {
					auto size = min(n, remaining);
					chunk += m_ram->read(0xa0000000 | (buffer + i), size);
					remaining -= n;
					update_progress(offset + i, size);
				}
#else
				string chunk = m_ram->read(0xa0000000 | buffer, length);
#endif
				return chunk;
			}
		} while (t.elapsed() < 5000);

		throw runtime_error("timeout retrieving block " + to_string(block));
	}

	virtual std::string read_special(uint32_t, uint32_t) override
	{ throw runtime_error(__func__); }

	private:
	sp m_ram;
	unsigned m_image;
	uint32_t m_cmstate_saved;
	uint32_t m_cpuc_reg_request;
	uint32_t m_mbox_reg_cmstate;
	uint32_t m_mbox_reg_imgreq;
	uint32_t m_mbox_reg_imgbuf;
	uint32_t m_cmstate_value;
	uint32_t m_request_value;
};
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
			offset += completed;
			length -= completed;
			logger::v() << "resuming at offset 0x" + to_hex(offset) << endl;
			os.seekp(completed, ios::cur);
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

					if (++retries < 5 /*&& wait_for_interface(interface())*/) {
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
		} else if (bootloader_flash::is_supported(intf, space)) {
			return create_rwx<bootloader_flash>(intf, space);
		}
	} else if (intf->name() == "bfc") {
		if (space.is_mem()) {
			return create_rwx<bfc_ram>(intf, space);
		} else if (!safe && bfc_flash2::is_supported(intf, space.name())) {
			return create_rwx<bfc_flash2>(intf, space);
		} else if (!safe && bfc_bootassist::is_supported(intf, space)) {
			return create_rwx<bfc_bootassist>(intf, space);
		} else {
			return create_rwx<bfc_flash>(intf, space);
		}
#ifdef BCM2DUMP_WITH_SNMP
	} else if (intf->name() == "snmp") {
		auto p = dynamic_pointer_cast<snmp>(intf);
		if (!p) {
			throw runtime_error("non-snmp interface");
		}

		if (!safe && bfc_bootassist::is_supported(intf, space)) {
			return create_rwx<bfc_bootassist>(intf, space);
		}

		auto ret = p->create_rwx(space, safe);
		// FIXME this should be moved to create_rwx
		ret->set_interface(intf);
		ret->set_addrspace(space);
		return ret;
	}
#else
	}
#endif

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

/**
 * bcm2-utils
 * Copyright (C) 2016 Joseph C. Lehner <joseph.c.lehner@gmail.com>
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

#include <iostream>
#include <stdexcept>
#include <sstream>
#include <string>
#include "bcm2dump.h"
using namespace std;

typedef runtime_error user_error;

string trim(const string& str)
{
	auto len = str.find_last_not_of(" \t\r\n");
	if (len == string::npos) {
		return "";
	}

	auto beg = str.find_first_not_of(" \t\r\n");
	if (beg == string::npos) {
		beg = 0;
	}

	return str.substr(beg, len);
}

template<class T> string to_hex(const T& t)
{
	ostringstream ostr;
	ostr << hex << t;
	return ostr.str();
}

class bcm2stream
{
	public:
	virtual ~bcm2stream() {}

	virtual string readln(unsigned timeout = 0) const = 0;
	virtual void writeln(const string& buf = "") const = 0;
	virtual void write(const string& buf) const = 0;

	virtual bool has_pending(unsigned timeout = 100) const = 0;

	virtual void iflush() const = 0;
};


struct params
{
	uint32_t offset;
	uint32_t length;
	bcm2_partition* partition;
	bcm2_addrspace* space;
	const char* filename;
};

class interface
{
	public:
	virtual void set_stream(bcm2stream& io);
	virtual bool is_active() const;
	virtual bool is_ready() const;

	virtual string read_mem(size_t addr, size_t len);

	virtual void dump(const params& p);
	virtual void write(const params& p);
	virtual void runcmd(const string& cmd) const;

	virtual void exec(const params& p);

	protected:

	bcm2stream& m_io;
};

class dumper
{
	public:
#if 0
	virtual ~dumper()
	{ cleanup(); }
#endif

	virtual void set_params(const params& p)
	{ m_params = p; }

	virtual uint32_t get_alignment() const
	{ return 4; }

	virtual uint32_t get_chunk_size() const = 0;

	void dump() const
	{

	}

	string read_chunk(uint32_t off, uint32_t len = 0) const
	{ 
		read_chunk_impl(off, len, 0);
	}

	protected:
	virtual void prepare() const {}
	virtual void send_read_chunk_cmd(uint32_t len = 0) const = 0;
	virtual void cleanup() const {}

	virtual bool parse_chunk_line(const string& line, string& buf, uint32_t offset) const = 0;
	virtual bool is_non_chunk_line(const string& line) const = 0;

	void writeln(const string& str)
	{
		m_io.writeln(str);
	}

	params m_params;
	interface m_intf;
	bcm2stream& m_io;

	private:

	string read_chunk_impl(uint32_t off, uint32_t len, uint32_t retries) const
	{
		if (!len) {
			len = get_chunk_size();
		}

		send_read_chunk_cmd(len);

		string line, linebuf, chunk;
		uint32_t pos = off;

		while (chunk.size() < len && m_io.has_pending()) {
			line = trim(m_io.readln(100));

			if (is_non_chunk_line(line)) {
				continue;
			} else if (parse_chunk_line(line, linebuf, off)) {
				pos += linebuf.size();
				chunk += linebuf;
				// TODO check offset
			} else {
				break;
			}
		}

		if (chunk.size() != len) {
			if (retries >= 2) {
				throw runtime_error("failed to read chunk (@" + to_string(off)
						+ ", " + to_string(len) + ")");
			}
				
			// TODO log
			return read_chunk_impl(off, len, retries + 1);
		}

		return chunk;
	}
};

class bl_ram_dumper : public dumper
{
	public:
	virtual uint32_t get_chunk_size() const
	{ return 4; }

	protected:

	virtual bool is_non_chunk_line(const string& line) const
	{ return line.find("Value at") == string::npos; }


	virtual void send_read_chunk_cmd(uint32_t off, uint32_t len) const
	{
		m_io.writeln(to_hex(off));
	}

	virtual bool parse_chunk_line(const string& line, string& buf, uint32_t off)
	{
		istringstream istr(line.substr(9));

		uint32_t val = 0;
		char c;

		if (!(istr >> hex >> val) || val != off || (!istr >> c) || c != ':') {
			return false;
		}

		if (!(istr >> hex >> val)) {
			return false;
		}

		val = htonl(val);
		buf += string(reinterpret_cast<char*>(&val), 4);
	}

	virtual void prepare()
	{
		m_intf.runcmd("r");
	}

	virtual void cleanup()
	{
		m_io.writeln();
	}
};

class cm_flash_dumper : public dumper
{
	public:
	virtual uint32_t get_chunk_size() const
	{ return 2048; }

	protected:
	virtual void send_read_chunk_cmd(uint32_t off, uint32_t len) const
	{
		off -= m_params.partition->offset;
		m_intf.runcmd("/flash/readDirect " + to_string(len)
				+ to_string(off));
	}

	virtual bool is_non_chunk_line(const string& line)
	{
		return line.empty() || line.find("/flash/") != string::npos;
	}

	virtual bool parse_chunk_line(const string& line, string& buf, uint32_t off)
	{
		uint8_t val = 0;
		istringstream istr(line);

		for (unsigned i = 0; i < 16; ++i) {
			if (!(istr >> hex >> val)) {
				return false;
			}

			buf += char(val);
		}

		return true;
	}

	virtual void prepare()
	{
		const bcm2_partition* p = m_params.partition;
		if (!p) {
			throw user_error("dumping by offset is not supported");
		}

		cleanup();
		m_intf.runcmd("/flash/open " + string(p->altname[0] ? p->altname : p->name));

		bool opened = false;

		while (m_io.has_pending()) {
			string line = m_io.readln();
			if (line.find("opened") != string::npos) {
				break;
			}
		}

		if (!opened) {
			throw runtime_error("failed to open partition");
		}
	}

	virtual void cleanup()
	{
		m_intf.runcmd("/flash/close");
		while (m_io.has_pending()) {
			m_io.readln();
		}
	}
};

class cm_ram_dumper : public dumper
{
	public:
	virtual uint32_t get_chunk_size() const
	{ return 1024; }	

	protected:
	virtual void send_read_chunk_cmd(uint32_t off, uint32_t len) const
	{
		m_intf.runcmd("/read_memory -s 4 -n " + to_string(len)
				+ " " + to_string(off));
	}

	virtual bool is_non_chunk_line(const string& line)
	{
		return line.empty() || line.size() != 67;
	}

	virtual bool parse_chunk_line(const string& line, string& buf, uint32_t off)
	{
		istringstream istr(line);
		uint32_t val = 0;
		char c = 0;

		if (!(istr >> hex >> val >> c) || val != off || c != ':') {
			return false;
		}

		for (unsigned i = 0; i < 4; ++i) {
			if (!(istr >> hex >> val)) {
				return false;
			}

			val = htonl(val);
			buf += string(reinterpret_cast<char*>(&val), 4);
		}

		return true;
	}
};

class cminterface : public interface
{
	public:
	virtual string read_mem(size_t addr, size_t len)
	{
			


	}	

};

/**
 * bcm2-utils
 * Copyright (C) 2016-2024 Joseph Lehner <joseph.c.lehner@gmail.com>
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

#include <stdexcept>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include "interface.h"
#include "progress.h"
#include "rwx.h"
#include "io.h"
using namespace std;
using namespace bcm2dump;

#ifndef VERSION
#define VERSION "v(unknown)"
#endif

namespace {

const unsigned opt_resume = 1;
const unsigned opt_force = (1 << 1);
const unsigned opt_safe = (1 << 2);
const unsigned opt_force_write = (1 << 3);

void usage(bool help = false)
{
	ostream& os = logger::i();

	os << "Usage: bcm2dump [<options>] <command> [<arguments> ...]" << endl;
	os << endl;
	os << "Options:" << endl;
	os << "  -s               Always use safe (and slow) methods" << endl;
	os << "  -R               Resume dump" << endl;
	os << "  -F               Force operation" << endl;
	os << "  -P <profile>     Force profile" << endl;
	os << "  -L <filename>    I/O log file" << endl;
	os << "  -O <opt>=<val>   Override option value" << endl;
	os << "  -q               Decrease verbosity" << endl;
	os << "  -v               Increase verbosity" << endl;
	os << endl;
	os << "Commands: " << endl;
	os << "  dump  <interface> <addrspace> {<partition>[+<off>],<off>}[,<size>] <out>" << endl;
	if (help) {
		os << "\n    Dump data from given address space, starting at an explicit offset\n"
				"    or alternately a partition name. If a partition name is used, the\n"
				"    <size> argument may be omitted. Data is stored in file <out>.\n\n";
	}
	os << "  scan  <interface> <addrspace> <step> [<start> <size>]" << endl;
	if (help) {
		os << "\n    Scan given address space for image headers, in steps of <step> bytes.\n"
				"    For unknown profiles or address spaces, <start> and <size> must be\n"
				"    specified.\n\n";
	}
	os << "  write <interface> <addrspace> {<partition>[+<off>],<off>}[,<size>] <in>" << endl;
	if (help) {
		os << "\n    Write data to the specified address space, starting at an explicit\n"
				"    offset or alternately a partition name. The <size> argument may be\n"
				"    specified to use only that number of bytes of file <in>.\n\n";
	}
	os << "  exec  <interface> <off>[,<entry>] <in>" << endl;
	if (help) {
#if 0
		os << "\n    Write data to ram at the specified offset. After the data has been written,\n"
				"    execute the code and print all subsequently generated output to standard output.\n"
				"    An <entry> argument may be supplied to start execution at a different address.\n\n";
#else
		os << "\n    Write data to ram at the specified offset. After the data has been\n"
				"    written, execute the code. An <entry> argument may be supplied to\n"
				"    start execution at a different address.\n\n";
#endif
	}
	os << "  run   <interface> <command 1> [<command 2> ...]" << endl;
	if (help) {
		os << "\n    Run command(s) on the specified interface.\n\n";
	}
	os << "  info  <interface>" << endl;
	if (help) {
		os << "\n    Print information about a profile. In the absence of a -P flag, use\n"
				"    auto-detection.\n\n";
	}
	os << "  help" << endl;
	if (help) {
		os << "\n    Print this information and exit.\n";
	}
	os << endl;
	os << "Interfaces: " << endl;
#ifndef _WIN32
	os << "  /dev/ttyUSB0             Serial console with default baud rate" << endl;
	os << "  /dev/ttyUSB0,115200      Serial console, 115200 baud" << endl;
#else
	os << "  COM1                     Serial console with default baud rate" << endl;
	os << "  COM1,115200              Serial console, 115200 baud" << endl;
#endif
	os << "  192.168.0.1,2323         Raw TCP connection to 192.168.0.1, port 2323" << endl;
	os << "  192.168.0.1,foo,bar      Telnet, server 192.168.0.1, user 'foo'," << endl;
	os << "                           password 'bar'" << endl;
	os << "  192.168.0.1,foo,bar,233  Same as above, port 233" << endl;
#ifdef BCM2DUMP_WITH_SNMP
	os << "  snmp:192.168.100.1       SNMP interface at 192.168.100.1" << endl;
#endif
	os << endl;
	os << "Profiles:" << endl;
	os << get_profile_names(70, 2) << endl;
	os << endl;
	os << "bcm2dump " << VERSION <<" Copyright (C) 2016-2024 Joseph C. Lehner" << endl;
	os << "Licensed under the GNU GPLv3; source code is available at" << endl;
	os << "https://github.com/jclehner/bcm2-utils" << endl;
	os << endl;
}

void handle_exception(const exception& e, bool io_log = true)
{
	logger::e() << endl << "error: " << e.what() << endl;

	if (io_log) {
		auto lines = logger::get_last_io_lines();
		if (!lines.empty()) {
			logger::d() << endl;
			logger::d() << "context:" << endl;

			for (string line : lines) {
				logger::d() << "  " << line << endl;
			}

			logger::d() << endl;
		}
	}
}

void handle_sigint()
{
	logger::w() << endl << "interrupted" << endl;
}

void image_listener(uint32_t offset, const ps_header& hdr)
{
	logger::i("  %s (0x%04x, %d b)\n", hdr.filename().c_str(), hdr.signature(), hdr.length());
}

class progress_listener
{
	public:
	progress_listener(const string& prefix, char** argv, uint32_t offset = 0, uint32_t length = 0)
	: m_prefix(prefix), m_argv(argv), m_offset(offset), m_length(length) {}

	void operator()(uint32_t offset, uint32_t length, bool write, bool init)
	{
		if (init && !m_skip_init) {
			if (m_length) {
				offset = m_offset;
				length = m_length;
				m_skip_init = true;
			} else {
				m_offset = offset;
				m_init_length = length;
			}

			progress_init(&m_pg, offset, length);
			if (m_argv[2] != "special"s) {
				logger::i("%s %s:0x%08x-0x%08x (%d b)\n", m_prefix.c_str(), m_argv[2], m_pg.min, m_pg.max, m_pg.max + 1 - m_pg.min);
			} else {
				logger::i("%s %s\n", m_prefix.c_str(), m_argv[3]);
			}
		}

		logger::i("\r ");

		if (offset != UINT32_MAX) {
			progress_set(&m_pg, offset);
		}

		progress_print(&m_pg, stdout);

		if (is_end(offset, length)) {
			logger::i("\n");
		}

		//logger::i("o=0x%08x, l=%u, mo=0x%08x, ml=%u %s\n", offset, length, m_offset, m_length, init ? "init" : "");
	}

	private:
	bool is_end(uint32_t offset, uint32_t length)
	{
		return ((m_length || m_init_length) && (offset == (m_offset + (m_length ? m_length : m_init_length))))
			|| (offset == UINT32_MAX && length == UINT32_MAX);
	}

	string m_prefix;
	char** m_argv;
	uint32_t m_offset, m_length, m_init_length;
	bool m_skip_init = false;
	progress m_pg;
};

unsigned parse_width(const string& str)
{
	auto width = lexical_cast<unsigned>(str, 0);
	if (!width || width == 3 || width > 4) {
		throw user_error("invalid width: " + to_string(width));
	}

	return width;
}

bool run_script_command(vector<string> args, sp<rwx> rwx)
{
	if (args.empty()) {
		return true;
	}

	if (args[0] == "read") {
		if (args.size() < 2 || args.size() > 3) {
			throw user_error("usage: read <address> [<size>]");
		}

		auto addr = lexical_cast<uint32_t>(args[1], 0);
		auto width = (args.size() == 3 ? parse_width(args[2]) : 4);

		string buf = rwx->read(addr, width);
		uint32_t result;

		if (width == 4) {
			result = be_to_h(extract<uint32_t>(buf));
		} else if (width == 2) {
			result = be_to_h(extract<uint16_t>(buf));
		} else {
			result = buf[0];
		}

		cout << to_hex(result, width * 2) << endl;
	} else if (args[0] == "write") {
		if (args.size() != 4) {
			throw user_error("usage: write <address> <size> <value>");
		}

		auto addr = lexical_cast<uint32_t>(args[1], 0);
		auto width = parse_width(args[2]);
		auto value = lexical_cast<uint32_t>(args[3], 0);

		string buf;

		if (width == 4) {
			buf = to_buf(h_to_be(value));
		} else if (width == 2) {
			buf = to_buf(h_to_be(uint16_t(value & 0xffff)));
		} else {
			buf += char(value & 0xff);
		}

		rwx->write(addr, buf);
	} else if (args[0] == "exec") {
		if (args.size() != 2) {
			throw user_error("usage: exec <address>");
		}

		rwx->exec(lexical_cast<uint32_t>(args[1], 0));
	} else if (args[0] == "quit" || args[0] == "exit") {
		return false;
	} else {
		throw user_error("invalid command: " + args[0]);
	}

	return true;
}

int do_script(int argc, char** argv, int opts, const string& profile)
{
	if (argc < 3) {
		usage(false);
		return 1;
	}

	auto intf = interface::create(argv[1], profile);
	auto rwx = rwx::create(intf, argv[2], opts & opt_safe);

	if (argc == 3) {
		// we're in interactive mode
		string line;
		while (cout << "bcm2dump> " << flush, getline(cin, line)) {
			auto args = split(line, ' ');

			try {
				if (!run_script_command(args, rwx)) {
					break;
				}
			} catch (const exception& e) {
				logger::e() << "error: " << e.what() << endl;
			}
		}
	} else {
		vector<string> args;
		for (int i = 3; i < argc; ++i) {
			args.push_back(argv[i]);
		}

		run_script_command(args, rwx);
	}

	return 0;
}


int do_dump(int argc, char** argv, int opts, const string& profile)
{
	if (argc != 5) {
		usage(false);
		return 1;
	}

	if (access(argv[4], F_OK) == 0 && !(opts & (opt_force | opt_resume))) {
		throw user_error("output file "s + argv[4] + " exists; specify -F to overwrite or -R to resume dump");
	}

	auto intf = interface::create(argv[1], profile);
	rwx::sp rwx;

	if (argv[2] != "special"s) {
		rwx = rwx::create(intf, argv[2], opts & opt_safe);
	} else {
		rwx = rwx::create_special(intf, argv[3]);
	}

	if (logger::loglevel() <= logger::info) {
		rwx->set_progress_listener(progress_listener("dumping", argv));
		rwx->set_image_listener(&image_listener);
	}

	ios::openmode mode = ios::out | ios::binary;
	if (opts & opt_resume) {
		// without ios::in, the file will be overwritten!
		mode |= ios::in;
	} else if (opts & opt_force) {
		mode |= ios::trunc;
	}

	ofstream of(argv[4], mode);
	if (!of.good()) {
		throw user_error("failed to open "s + argv[4] + " for writing");
	}

	if (argv[2] != "special"s) {
		if (argv[3] != "dumpcode"s) {
			rwx->dump(argv[3], of, opts & opt_resume);
		} else {
			rwx->dump(intf->version().codecfg()["rwcode"] | intf->profile()->kseg1(), 512, of);
		}
	} else {
		rwx->dump(0, 0, of);
	}
	return 0;
}

int do_write_exec(int argc, char** argv, int opts, const string& profile)
{
	bool exec = (argv[0] == "exec"s);
	uint32_t entry = 0, loadaddr = 0;

	if ((!exec && argc != 5) || (exec && argc != 4)) {
		usage(false);
		return 1;
	}

	if (exec) {
		auto tok = split(argv[2], ',');
		if (tok.empty() || tok.size() > 2) {
			throw user_error("invalid argument '"s + argv[3] + "'");
		}

		loadaddr = lexical_cast<uint32_t>(tok[0], 0);
		entry = (tok.size() == 2 ? lexical_cast<uint32_t>(tok[1], 0) : loadaddr);
	}

	if (!exec && !(opts & opt_force_write) && argv[2] != "ram"s) {
		throw user_error("writing to non-ram address space "s + argv[2] + " is dangerous; specify -FF to continue");
	}

	string file = exec ? argv[3] : argv[4];
	ifstream in(file.c_str(), ios::binary);
	if (!in.good()) {
		throw user_error("failed to open " + file + " for reading");
	}

	auto intf = interface::create(argv[1], profile);
	auto rwx = rwx::create(intf, exec ? "ram" : argv[2], opts & opt_safe);

	progress pg;

	if (logger::loglevel() <= logger::info) {
		rwx->set_progress_listener([&pg, &argv] (uint32_t offset, uint32_t length, bool write, bool init) {
			if (init) {
				progress_init(&pg, offset, length);
				logger::i("%s %s:0x%08x-0x%08x (%d b)\n", write ? "writing" : "reading", argv[2], pg.min, pg.max, pg.max + 1 - pg.min);
			}

			logger::i("\r ");
			progress_set(&pg, offset);
			progress_print(&pg, stdout);
		});
	}

	if (exec) {
		rwx->write(loadaddr, in);
		logger::i("\n");
		logger::i("executing code at %08x\n", entry);
		rwx->exec(entry);
		// TODO print all subsequent output ?
	} else {
		rwx->write(argv[3], in);
		logger::i("\n");
	}

	return 0;
}

int do_run(int argc, char** argv, const string& profile)
{
	if (argc < 3) {
		usage(false);
		return 1;
	}

	auto intf = interface::create(argv[1], profile);
	auto cli = dynamic_pointer_cast<cmdline_interface>(intf);
	if (!cli) {
		throw user_error("not a commandline interface");
	}

	for (int i = 2; i < argc; ++i) {
		for (auto line : cli->run(argv[i])) {
			cout << trim(line) << endl;
		}
	}

	return 0;
}

int do_info(int argc, char** argv, const string& profile)
{
	if (argc != 1 && argc != 2) {
		usage(false);
		return 1;
	}

	if (argc == 2) {
		auto intf = interface::create(argv[1], profile);
		if (intf->profile()) {
			intf->profile()->print_to_stdout();
		}
	} else if (argc == 1 && !profile.empty()) {
		profile::get(profile)->print_to_stdout();
	} else {
		usage(false);
		return 1;
	}
	return 0;
}

int do_scan(int argc, char** argv, int opts, const string& profile)
{
	if (argc != 4 && argc != 6) {
		usage(false);
		return 1;
	}

	auto intf = interface::create(argv[1], profile);
	auto rwx = rwx::create(intf, argv[2], opts & opt_safe);

	if (!intf->profile() && argc != 6) {
		throw user_error("unknown profile, must specify <start> and <size>");
	}

	uint32_t start;
	uint32_t length;

	if (argc == 6) {
		start = lexical_cast<uint32_t>(argv[4], 0);
		length = lexical_cast<uint32_t>(argv[5], 0);
	} else {
		start = rwx->space().min();
		length = rwx->space().size();
	}

	uint32_t step = lexical_cast<uint32_t>(argv[3], 0);

	if (logger::loglevel() <= logger::info) {
		uint32_t scan_length = ((length / step) - 1) * step + 92;
		rwx->set_progress_listener(progress_listener("scanning", argv, start, scan_length));
	}

	map<uint32_t, ps_header> imgs;

	for (uint32_t offset = start; offset < (start + length); offset += step) {
		ps_header hdr(rwx->read(offset, 92));
		if (hdr.hcs_valid()) {
			//image_listener(offset, hdr);
			imgs[offset] = hdr;
		}
	}

	if (!imgs.empty()) {
		logger::i("\n\ndetected %u image(s) in range %s:0x%08x-0x%08x:\n", static_cast<unsigned>(imgs.size()), argv[2], start, start + length);
	}

	for (auto img : imgs) {
		logger::i("  0x%08x-0x%08x  %s\n", img.first, img.first + img.second.length() + 92,
				img.second.filename().c_str());
	}

	return 0;
}

}

int do_main(int argc, char** argv)
{
	ios_base::sync_with_stdio();
	string profile;
	int loglevel = logger::info;
	int opts = 0;
	int opt;

#ifdef _WIN32
	WSADATA wsa;
	if (WSAStartup(0x0202, &wsa) != 0) {
		throw winsock_error("WSAStartup");
	}
#endif

	opterr = 0;

	while ((opt = getopt(argc, argv, "hsARFqvP:L:O:")) != -1) {
		switch (opt) {
		case 's':
			opts |= opt_safe;
			break;
		case 'v':
			loglevel = max(loglevel - 1, logger::trace);
			break;
		case 'q':
			loglevel = min(loglevel + 1, logger::err);
			break;
		case 'F':
			if (opts & opt_force) {
				opts |= opt_force_write;
			} else {
				opts |= opt_force;
			}
			break;
		case 'R':
			opts |= opt_resume;
			break;
		case 'P':
			profile = optarg;
			break;
		case 'O':
			profile::parse_opt_override(optarg);
			break;
		case 'L':
			logger::set_logfile(optarg);
			break;
		case 'h':
		default:
			bool help = (opt == 'h' || (optopt == '-' && argv[optind] == "help"s));
			usage(help);
			return help ? 0 : 1;
		}
	}

	string cmd = optind < argc ? argv[optind] : "";
	if (cmd.empty() || cmd == "help") {
		usage(!cmd.empty());
		return 0;
	}

	logger::loglevel(loglevel);

	argv += optind;
	argc -= optind;

	if (cmd == "run" || cmd == "script") {
		logger::no_stdout();
	}

	logger::d() << "bcm2dump " << VERSION << endl;

	if (cmd == "info") {
		return do_info(argc, argv, profile);
	} else if (cmd == "run") {
		return do_run(argc, argv, profile);
	} else if (cmd == "dump") {
		return do_dump(argc, argv, opts, profile);
	} else if (cmd == "write" || cmd == "exec") {
		return do_write_exec(argc, argv, opts, profile);
	} else if (cmd == "scan") {
		return do_scan(argc, argv, opts, profile);
	} else if (cmd == "script") {
		return do_script(argc, argv, opts, profile);
	} else {
		usage(false);
		return 1;
	}
}

int main(int argc, char** argv)
{
	try {
		return do_main(argc, argv);
	} catch (const rwx::interrupted& e) {
		handle_sigint();
	} catch (const errno_error& e) {
		if (!e.interrupted()) {
			handle_exception(e);
		} else {
			handle_sigint();
		}
	} catch (const user_error& e) {
		handle_exception(e, false);
	} catch (const exception& e) {
		handle_exception(e);
	}

	return 1;
}

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
	os << "  -q               Decrease verbosity" << endl;
	os << "  -v               Increase verbosity" << endl;
	os << endl;
	os << "Commands: " << endl;
	os << "  dump  <interface> <addrspace> {<partition>[+<offset>],<offset>}[,<size>] <out>" << endl;
	if (help) {
		os << "\n    Dump data from given address space, starting at either an explicit offset or\n"
				"    alternately a partition name. If a partition name is used, the <size>\n"
				"    argument may be omitted. Data is stored in file <out>.\n\n";
	}
	os << "  scan  <interface> <addrspace> <step> [<start> <size>]" << endl;
	if (help) {
		os << "\n    Scan given address space for image headers, in steps of <step> bytes. For unknown\n"
				"    profiles or address spaces, <start> and <size> must be specified.\n\n";
	}
	os << "  write <interface> <addrspace> {<partition>[+<offset>],<offset>}[,<size>] <in>" << endl;
	if (help) {
		os << "\n    Write data to the specified address space, starting at either an explicit\n"
				"    offset or alternately a partition name. The <size> argument may be used to\n"
				"    use only parts of file <in>.\n\n";
	}
	os << "  exec  <interface> {<partition>,<offset>}[,<entry>] <in>" << endl;
	if (help) {
		os << "\n    Write data to ram, starting at either an explicit offset or alternately a\n"
				"    partition name. After the data has been written, execute the code and print\n"
				"    all subsequently generated output to standard output. An <entry> argument\n"
				"    may be supplied to start execution at a different address.\n\n";
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
	os << endl;
	os << "bcm2dump " << VERSION << " Copyright (C) 2016 Joseph C. Lehner" << endl;
	os << "Licensed under the GNU GPLv3; source code is available at" << endl;
	os << "https://github.com/jclehner/bcm2utils" << endl;
	os << endl;
}

void handle_exception(const exception& e, bool io_log = true)
{
	logger::e() << endl << "error: " << e.what() << endl;

	if (io_log) {
		auto lines = io::get_last_lines();
		if (!lines.empty()) {
			logger::d() << endl;
			logger::d() << "context:" << endl;

			for (string line : io::get_last_lines()) {
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
	printf("  %s (0x%04x, %d b)\n", hdr.filename().c_str(), hdr.signature(), hdr.length());
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
			}

			progress_init(&m_pg, offset, length);
			if (m_argv[2] != "special"s) {
				printf("%s %s:0x%08x-0x%08x (%d b)\n", m_prefix.c_str(), m_argv[2], m_pg.min, m_pg.max, m_pg.max + 1 - m_pg.min);
			} else {
				printf("%s %s\n", m_prefix.c_str(), m_argv[3]);
			}
		}

		printf("\r ");
		progress_set(&m_pg, offset);
		progress_print(&m_pg, stdout);
	}

	private:
	string m_prefix;
	char** m_argv;
	uint32_t m_offset, m_length;
	bool m_skip_init = false;
	progress m_pg;
};


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
			rwx->dump(intf->profile()->codecfg(intf->id()).loadaddr | intf->profile()->kseg1(), 512, of);
		}
	} else {
		rwx->dump(0, 0, of);
	}
	return 0;
}

int do_write(int argc, char** argv, int opts, const string& profile)
{
	if (argc != 5) {
		usage(false);
		return 1;
	}

	if (!(opts & opt_force_write) && argv[2] != "ram"s) {
		throw user_error("writing to non-ram address space "s + argv[2] + " is dangerous; specify -FF to continue");
	}

	ifstream in(argv[4], ios::binary);
	if (!in.good()) {
		throw user_error("failed to open "s + argv[4] + " for reading");
	}

	auto intf = interface::create(argv[1]);
	auto rwx = rwx::create(intf, argv[2], opts & opt_safe);

	progress pg;

	if (logger::loglevel() <= logger::info) {
		rwx->set_progress_listener([&pg, &argv] (uint32_t offset, uint32_t length, bool write, bool init) {
			if (init) {
				progress_init(&pg, offset, length);
				printf("%s %s:0x%08x-0x%08x (%d b)\n", write ? "writing" : "reading", argv[2], pg.min, pg.max, pg.max + 1 - pg.min);
			}

			printf("\r ");
			progress_set(&pg, offset);
			progress_print(&pg, stdout);
		});
	}

	rwx->write(argv[3], in);
	return 0;
}

int do_info(int argc, char** argv, const string& profile)
{
	if (argc != 1 && argc != 2) {
		usage(false);
		return 1;
	}

	if (argc == 2) {
		auto intf = interface::create(argv[1]);
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

	auto intf = interface::create(argv[1]);
	auto rwx = rwx::create(intf, argv[2], opts & opt_safe);

	if (!intf->profile() && argc != 6) {
		throw user_error("unknown profile, must specify <start> and <size>");
	}

	uint32_t start = rwx->space().min();
	uint32_t length = rwx->space().size();
	uint32_t step = lexical_cast<uint32_t>(argv[3]);

	if (argc == 6) {
		start = lexical_cast<uint32_t>(argv[4]);
		length = lexical_cast<uint32_t>(argv[5]);
	}

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
		printf("\n\ndetected %zu image(s) in range %s:0x%08x-0x%08x:\n", imgs.size(), argv[2], start, start + length);
	}

	for (auto img : imgs) {
		printf("  0x%08x-0x%08x  %s\n", img.first, img.first + img.second.length() + 92,
				img.second.filename().c_str());
	}

	return 0;
}

}

int main(int argc, char** argv)
{
	ios_base::sync_with_stdio();
	string profile;
	int loglevel = logger::info;
	int opts = 0;
	int opt;

	opterr = 0;

	while ((opt = getopt(argc, argv, "hsARFqvP:")) != -1) {
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

	try {
		if (cmd == "info") {
			return do_info(argc, argv, profile);
		} else if (cmd == "dump") {
			return do_dump(argc, argv, opts, profile);
		} else if (cmd == "write") {
			return do_write(argc, argv, opts, profile);
		} else if (cmd == "scan") {
			return do_scan(argc, argv, opts, profile);
		} else {
			usage(false);
		}
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

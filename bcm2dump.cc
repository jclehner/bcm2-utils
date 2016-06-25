#include <stdexcept>
#include <iostream>
#include <fstream>
#include "interface.h"
#include "bcm2dump.h"
#include "rwx.h"
#include "io.h"
using namespace std;
using namespace bcm2dump;

namespace {
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
	os << "  dump  <interface> <addrspace> {<partition>,<offset>}[,<size>] <outfile>" << endl;
	if (help) {
		os << "\n    Dump data from given address space, starting at either an explicit offset or\n"
				"    alternately a partition name. If a partition name is used, the <size>\n"
				"    argument may be omitted. Data is stored in <outfile>.\n\n";
	}
	os << "  write <interface> <addrspace> {<partition>,<offset>}[,<size>] <infile>" << endl;
	if (help) {
		os << "\n    Write data to the specified address space, starting at either an explicit\n"
				"    offset or alternately a partition name. The <size> argument may be used to\n"
				"    use only parts of <infile>.\n\n";
	}
	os << "  exec  <interface> {<partition>,<offset>}[,<entry>] <infile>" << endl;
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
	os << "Interface: " << endl;
	os << "  serial:/dev/ttyUSB0             Serial console with default baud rate" << endl;
	os << "  serial:/dev/ttyUSB0,115200      Serial console, 115200 baud" << endl;
	os << "  tcp:192.168.0.1,2323            Raw TCP connection to 192.168.0.1, port 2323" << endl;
	os << "  telnet:192.168.0.1,foo,bar      Telnet connection to 192.168.0.1, user 'foo'," << endl;
	os << "                                  password 'bar', default port (23)" << endl;
	os << "  telnet:192.168.0.1,foo,bar,233  Same as above, port 233" << endl;
	os << endl;
	os << "Type prefixes ('serial:', 'tcp:', etc.) can usually be omitted." << endl;
}

int do_dump(int argc, char** argv, bool safe)
{
	auto intf = interface::create(argv[0]);
	rwx::sp rwx;

	if (argv[1] != "special"s) {
		rwx = rwx::create(intf, argv[1], safe);
	} else {
		rwx = rwx::create_special(intf, argv[2]);
	}

	progress pg;

	if (logger::loglevel() <= logger::info) {
		rwx->set_progress_listener([&pg, &argv] (uint32_t offset, uint32_t length, bool init) {
			if (init) {
				progress_init(&pg, offset, length);
				printf("dumping %s:0x%08x-0x%08x\n", argv[2], pg.min, pg.max);
			}

			printf("\r ");
			progress_set(&pg, offset);
			progress_print(&pg, stdout);
		});

		rwx->set_image_listener([] (uint32_t offset, const ps_header& hdr) {
			printf("  %s  (%u b)\n", hdr.filename().c_str(), hdr.length());
		});
	}

	ofstream of(argv[3]);
	if (!of.good()) {
		throw runtime_error("failed to open "s + argv[3]);
	}

	if (argv[1] != "special"s) {
		if (argv[2] != "dumpcode"s) {
			rwx->dump(argv[2], of);
		} else {
			rwx->dump(intf->profile()->codecfg(intf->id()).loadaddr | intf->profile()->kseg1(), 512, of);
		}
	} else {
		rwx->dump(0, 0, of);
	}
	logger::i() << endl;
	return 0;
}

int do_info(int argc, char** argv)
{
	auto intf = interface::create(argv[0]);
	if (intf->profile()) {
		intf->profile()->print_to_stdout();
	}
	return 0;
}

}

int main(int argc, char** argv)
{
	ios_base::sync_with_stdio();
	bool safe = false;
	int loglevel = logger::info;
	int opt;

	opterr = 0;

	while ((opt = getopt(argc, argv, "hsARFqvP:")) != -1) {
		switch (opt) {
		case 's':
			safe = true;
			break;
		case 'v':
			loglevel = min(loglevel - 1, logger::trace);
			break;
		case 'q':
			loglevel = max(loglevel + 1, logger::err);
			break;
		case 'A':
		case 'F':
		case 'R':
		case 'P':
			// FIXME
			logger::e() << "flag not implemented -" << opt << endl;
			break;
		case 'h':
		default:
			bool help = (optopt == 'h' || (optopt == '-' && argv[optind] == "help"s));
			usage(help);
			return help ? 0 : 1;
		}
	}

	string cmd = argv[optind];
	if (cmd == "help") {
		usage(true);
		return 0;
	}

	logger::loglevel(loglevel);

	argv += optind;
	argc -= optind;

	try {
		if (cmd == "info") {
			return do_info(argc, argv);
		} else if (cmd == "dump") {
			return do_dump(argc, argv, safe);
		} else {
			logger::e() << "command not implemented: " << cmd << endl;
			return 1;
		}
	} catch (const rwx::interrupted& e) {
		logger::w() << endl << "interrupted" << endl;
		return 1;
	} catch (const exception& e) {
		logger::e() << endl;
		logger::e() << "**************************" << endl;
		logger::e() << "error: " << e.what() << endl;

		auto lines = io::get_last_lines();
		if (!lines.empty()) {
			logger::v() << endl;
			logger::v() << "context:" << endl;

			for (string line : io::get_last_lines()) {
				logger::v() << "  " << line << endl;
			}

			logger::v() << endl;
		}
		return 1;
	}
}

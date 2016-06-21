#include <iostream>
#include <fstream>
#include "interface.h"
#include "bcm2dump.h"
#include "rwx.h"
#include "io.h"
using namespace std;
using namespace bcm2dump;

namespace {
}

int main(int argc, char** argv)
{
	ios_base::sync_with_stdio();

	if (argc != 5 && argc != 3) {
		cerr << "usage: bcm2dump <interface> <addrspace> <partition> <outfile>" << endl;
		cerr << endl;
		cerr << "interface specifications: " << endl;
		cerr << "  serial:/dev/ttyUSB0             serial console with default baud rate" << endl;
		cerr << "  serial:/dev/ttyUSB0,115200      serial console, 115200 baud" << endl;
		cerr << "  tcp:192.168.0.1,2323            raw tcp connection to 192.168.0.1, port 2323" << endl;
		cerr << "  telnet: 192.168.0.1,foo,bar     telnet connection to 192.168.0.1, user 'foo', pw 'bar', port 23" << endl;
		cerr << "  telnet:192.168.0.1,foo,bar,233  same as above, but with port 233" << endl;
		cerr << "  telnet:192.168.0.1,foo,b\\,     telnet connection, user 'foo', pw 'b,'" << endl;
		cerr << "  telnet:192.168.0.1,foo,b\\\\,3  telnet connection, user 'foo', pw 'b\\', port 3" << endl;
		cerr << endl;
		cerr << "the type prefixes can often be omitted" << endl;
		return 1;
	}

	try {
		logger::loglevel(logger::TRACE);

		auto intf = interface::create(argv[1]);
		rwx::sp rwx;

		if (argv[2] == "info"s) {
			if (intf->profile()) {
				intf->profile()->print_to_stdout();
			}
			return 0;
		} else if (argv[2] != "special"s) {
			rwx = rwx::create(intf, argv[2], argv[2] == "ram"s && argv[3] == "dumpcode"s);
		} else {
			rwx = rwx::create_special(intf, argv[3]);
		}
		progress pg;

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

		ofstream of(argv[4]);
		if (!of.good()) {
			cerr << "error: failed to open " << argv[4] << endl;
			return 1;
		}

		if (argv[2] != "special"s) {
			if (argv[3] != "dumpcode"s) {
				rwx->dump(argv[3], of);
			} else {
				rwx->dump(intf->profile()->codecfg(intf->id()).loadaddr | intf->profile()->kseg1(), 512, of);
			}
		} else {
			rwx->dump(0, 0, of);
		}
		cout << endl;
	} catch (const rwx::interrupted& e) {
		cerr << endl << "interrupted" << endl;
		return 1;
	} catch (const exception& e) {
		cerr << endl;
		cerr << "**************************" << endl;
		cerr << "error: " << e.what() << endl;
		cerr << endl;
		cerr << "context:" << endl;

		for (string line : io::get_last_lines()) {
			cerr << "  " << line << endl;
		}

		cerr << endl;
		return 1;
	}

#if 0
	const uint32_t offset = 0x85f00000;
	const uint32_t bytes = 128;

	rwx::sp d = rwx::create(intf, "ram");
	writer::sp w = writer::create(intf, "ram");

	srand(time(NULL));

	progress pg;
	progress_init(&pg, offset, bytes);

	for (unsigned i = 0; i < 16; ++i) {
		string data;
		for (unsigned i = 0; i < bytes; ++i) {
			// don't generate numbers that contain 'b', 'c', 'd' or 'e'
			// since these might be interpreted as bootloader commands
			data += char(rand() % 0xff) & ~0x88;
		}

		string phase = "write";

		auto listener = [&i, &phase, &pg] (uint32_t offset, uint32_t n) {
				printf("\r%s (%2u): ", phase.c_str(), i);
				progress_add(&pg, n);
				progress_print(&pg, stdout);
		};

		w->set_progress_listener(listener);
		d->set_progress_listener(listener);

		progress_init(&pg, offset, bytes);
		w->write(offset, data);

		printf("\n");

		phase = "read ";
		progress_init(&pg, offset, bytes);

		if (d->dump(offset, data.size()) != data) {
			cerr << endl <<"mismatch" << endl;
			break;
		}

		printf("\n");
	}

	cout << endl;
#endif
}

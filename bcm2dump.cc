#include <iostream>
#include <fstream>
#include "interface.h"
#include "bcm2dump.h"
#include "reader.h"
#include "io.h"
using namespace std;
using namespace bcm2dump;

namespace {
}

int main(int argc, char** argv)
{
	ios_base::sync_with_stdio();

	if (argc != 5) {
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
		cerr << "the type prefixes can usually be omitted" << endl;
		return 1;
	}

	try {
		logger::loglevel(logger::DEBUG);

		auto intf = interface::create(argv[1]);

#if 0
#if 0
		auto profile = profile::get("TC7200");
		auto intf = interface::create_telnet("192.168.0.1", 23, "foobar", "foobar", profile);
#else
		auto intf = interface::create_serial(argv[1], 115200);
#endif
#endif

		const addrspace& space = intf->profile()->space(argv[2], intf->id());
		reader::sp reader;

		// TODO move this to reader::create
		if (space.is_mem()) {
			reader = reader::create(intf, "ram");
		} else {
			reader = reader::create(intf, "flash");
		}

		const addrspace::part& part = space.partition(argv[3]);

		progress pg;
		progress_init(&pg, part.offset(), part.size());
		
		reader->set_image_listener([] (uint32_t offset, const ps_header& hdr) {
			printf("  %s  (%u b)\n", hdr.filename().c_str(), hdr.length());
		});

		reader->set_progress_listener([&pg, &argv] (uint32_t offset, uint32_t length, bool init) {
			static bool first = true;
			if (first) {
				printf("dumping %s:0x%08x-0x%08x\n", argv[2], pg.min, pg.max);
				first = false;
			}

			printf("\r ");
			progress_set(&pg, offset);
			progress_print(&pg, stdout);
		});

		ofstream of(argv[4]);
		if (!of.good()) {
			cerr << "error: failed to open " << argv[4] << endl;
			return 1;
		}

		reader->dump(part, of);
		cout << endl;
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
	} catch (const reader_writer::interrupted& e) {
		cerr << endl << "interrupted" << endl;
		return 1;
	}

#if 0
	const uint32_t offset = 0x85f00000;
	const uint32_t bytes = 128;

	reader::sp d = reader::create(intf, "ram");
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

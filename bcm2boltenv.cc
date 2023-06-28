/**
 * bcm2-utils
 * Copyright (C) 2016-2022 Joseph Lehner <joseph.c.lehner@gmail.com>
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

#include <boost/crc.hpp>
#include <iostream>
#include <fstream>
#include "crypto.h"
#include "util.h"
using namespace std;
using namespace bcm2utils;
using namespace bcm2dump;

// FIXME
// FIXME
// FIXME
// This is just a temporary tool to dump the contents of a
// BOLT environment variable store. Supporting for this format
// in bcm2cfg is planned in the near future!
// FIXME
// FIXME
// FIXME

namespace {

struct boltenv_header
{
	uint32_t tlv_cheat;
	uint32_t magic;
	uint32_t unk1;
	uint32_t unk2;
	uint32_t write_count;
	uint32_t size;
	uint32_t checksum;
} __attribute__((packed));

struct boltenv_var
{
	enum {
		TYPE_END = 0,
		TYPE_VAR,
		TYPE_BLOCK
	};

	enum {
		FLAG_TEMP = (1 << 0),
		FLAG_RO   = (1 << 1)
	};

	uint8_t type;

	union {
		struct {
			uint8_t size;
			uint8_t flags;
		} var;
		uint16_t block_size_be;
	};
} __attribute__((packed));
}

int main(int argc, char* argv[])
{
	try {
		ifstream in(argv[1]);
		boltenv_header header;

		if (in.readsome((char*)&header, sizeof(header)) != sizeof(header)) {
			throw user_error("failed to read file header");
		}

		if (header.tlv_cheat != 0x1a01 || header.magic != 0xbabefeed) {
			throw user_error("bad magic: 0x" + to_hex(header.tlv_cheat) + ", 0x"
					+ to_hex(header.magic));
		}

		cout << "unknown    : 0x" + to_hex(header.unk1) + ", 0x" + to_hex(header.unk2) << endl;
		cout << "write_count: " << header.write_count << endl;
		cout << "size       : " << header.size << endl;
		cout << "checksum" << endl;
		cout << "   reported: 0x" << to_hex(header.checksum)<< endl;

#if 1
		auto pos = in.tellg();

		auto buf = make_unique<char[]>(header.size);
		in.readsome(buf.get(), header.size);

		boost::crc_32_type crc;
		crc.process_bytes(buf.get(), header.size);

		cout << "   expected: 0x" << to_hex(crc.checksum()) << endl;
		in.seekg(pos);
#endif
		cout << "==========================" << endl;

		size_t offset = 0;

		while (in && (in.peek() != boltenv_var::TYPE_END)) {
			boltenv_var var;
			if (in.readsome((char*)&var, sizeof(var)) != sizeof(var)) {
				throw user_error("failed to read variable header");
			}

			if (var.type != boltenv_var::TYPE_VAR && var.type != boltenv_var::TYPE_BLOCK) {
				throw user_error("unknown variable type 0x" + to_hex(var.type));
			}

			size_t size = (var.type == boltenv_var::TYPE_VAR) ? (var.var.size - 1): be_to_h(var.block_size_be);

			string raw(size, '\0');
			if (in.readsome(&raw[0], size) != size) {
				throw user_error("failed to read variable data");
			}

			if (var.type == boltenv_var::TYPE_VAR) {
				if (var.var.flags & 0xfc) {
					cout << "[0x" + to_hex(var.var.flags) << "] ";
				} else if (var.var.flags & boltenv_var::FLAG_RO) {
					cout << "[ro] ";
				}
				cout << raw << endl;
			} else {
				cout << "@" << to_hex(offset) << ": " << size << " bytes" << endl;
				cout << raw << endl;
			}

			offset += (3 + size);
		}

		if (!in) {
			cerr << "warning: missing end-of-file marker" << endl;
		}

		return 0;
	} catch (const exception& e) {
		cerr << "error: " << e.what() << endl;
		return 1;
	}
}







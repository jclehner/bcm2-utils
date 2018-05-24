/**
 * bcm2-utils
 * Copyright (C) 2016-2018 Joseph Lehner <joseph.c.lehner@gmail.com>
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

#include <fstream>
#include "util.h"
#include "ps.h"
using namespace bcm2dump;
using namespace std;

namespace {
struct monolithic_header
{
	// 0x4d4f4e4f (MONO)
	uint32_t magic; // 0x4d4f4e4f (MONO)
	// ProgramStore signature
	uint16_t pssig;
	uint16_t unk1;
	// length including header
	uint32_t length;
	uint16_t unk2;
	uint16_t unk3;
} __attribute__((packed));

void do_extract(istream& in, const ps_header& ps, size_t length = 0)
{
	if (!length) {
		length = ps.length();
	}

	auto buf = make_unique<char[]>(length);

	if (!in.read(buf.get(), length)) {
		throw runtime_error("read error (data)");
	}

	ofstream out(ps.filename());

	out.write(reinterpret_cast<const char*>(ps.data()), sizeof(ps_header::raw));
	out.write(buf.get(), length);

	if (!out) {
		throw runtime_error("write error");
	}
}

void extract_single(istream& in)
{
	string hbuf(92, '\0');

	if (in.readsome(&hbuf[0], hbuf.size()) < hbuf.size()) {
		throw runtime_error("read error (header)");
	}

	ps_header ps;

	if (ps.parse(hbuf).hcs_valid()) {
		logger::i() << ps.filename() << ", " << ps.length() << " b" << endl;
		do_extract(in, ps);
	} else {
		logger::e() << "checksum error at offset " << hex << in.tellg() << endl;
	}
}

int do_main(int argc, char* argv[])
{
	logger::loglevel(logger::debug);

	if (argc < 3) {
		logger::e() << "Usage: psextract [infile] [offsets...]" << endl;
		return 1;
	}

	ifstream in(argv[1]);

	if (!in.good()) {
		throw user_error("failed to open input file");
	}

	for (int i = 2; i < argc; ++i) {
		if (!in.seekg(lexical_cast<unsigned>(argv[i], 0))) {
			throw user_error("bad offset "s + argv[i]);
		}

		extract_single(in);
	}

	return 0;
}
}

int main(int argc, char* argv[])
{
	try {
		return do_main(argc, argv);
	} catch (const exception& e) {
		logger::e() << "error: " << e.what() << endl;
		return 1;
	}
}

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

class mono_header
{
	public:
	struct raw
	{
		// 0x4d4f4e4f (MONO)
		uint32_t magic;
		// signature (similar to ProgramStore)
		uint16_t signature;
		uint16_t unk1;
		// length including header
		uint32_t length;
		uint16_t unk2;
		uint16_t unk3;
	} __attribute__((packed));

	mono_header& parse(const string& buf)
	{
		if (buf.size() < sizeof(raw)) {
			throw invalid_argument("buffer too small to contain valid header");
		}

		memcpy(&m_raw, buf.data(), sizeof(m_raw));
		return *this;
	}

	bool valid() const
	{ return ntoh(m_raw.magic) == 0x4d4f4e4f; }

	uint16_t signature() const
	{ return ntoh(m_raw.signature); }

	uint32_t length() const
	{ return ntoh(m_raw.length); }

	uint16_t unk1() const
	{ return ntoh(m_raw.unk1); }

	uint16_t unk2() const
	{ return ntoh(m_raw.unk2); }

	uint16_t unk3() const
	{ return ntoh(m_raw.unk3); }

	private:
	raw m_raw;
};

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

string read_hbuf(istream& in)
{
	string hbuf(sizeof(ps_header::raw), '\0');
	if (!in.read(&hbuf[0], hbuf.size())) {
		throw runtime_error("read error (header)");
	}

	return hbuf;
}

void extract_ps(istream& in, const ps_header& ps)
{
	logger::i("0x%07lx  ", long(in.tellg()) - sizeof(ps_header::raw));
	logger::i() << "image: " << ps.filename() << ", " << ps.length() << " b";
	logger::v(", %04x", ps.signature());
	logger::i() << endl;

	do_extract(in, ps);
}

bool extract_ps(istream& in)
{
	ps_header ps;

	if (!ps.parse(read_hbuf(in)).hcs_valid()) {
		return false;
	}

	extract_ps(in, ps);
	return true;
}

void extract_image(istream& in)
{
	ps_header ps;
	mono_header mono;

	streamoff beg = in.tellg();
	string hbuf = read_hbuf(in);

	if (ps.parse(hbuf).hcs_valid()) {
		extract_ps(in, ps);
	} else {
		logger::i("0x%07lx  ", long(beg & 0xffffffff));

		if (mono.parse(hbuf).valid()) {
			logger::i() << "monolithic, " << mono.length() << " b";
			logger::v(", %04x, ", mono.signature());
			logger::v("(%04x %04x %04x)", mono.unk1(), mono.unk2(), mono.unk3());
			logger::i() << endl;

			streamoff end = beg + mono.length();
			in.seekg(beg + sizeof(mono_header::raw));

			while (!in.eof() && in.tellg() < end && extract_ps(in)) {
				streamoff pos = in.tellg() - beg;
				in.seekg(beg + align_right(pos, 0xffff + 1));
			}
		} else if (hbuf[0] == 0x30 && (hbuf[1] & 0xff) == 0x82) {
			// add 7, because sizeof(type + len) is 4, and
			// sizeof(end-of-data) is 2. add 1 for next data.

			auto len = ntoh(extract<uint16_t>(hbuf, 2)) + 7;
			logger::i() << "asn.1 data, " << len << " b " << endl;

			in.seekg(beg + len);

			return extract_image(in);
		} else {
			logger::e() << "unknown image format" << endl;
		}
	}
}

int do_main(int argc, char* argv[])
{
	logger::loglevel(logger::debug);

	if (argc < 2) {
		logger::e() << "Usage: psextract <infile> [<offset1> ...]" << endl;
		return 1;
	}

	ifstream in(argv[1]);

	if (!in.good()) {
		throw user_error("failed to open input file");
	}

	if (argc == 2) {
		extract_image(in);
	} else {
		for (int i = 2; i < argc; ++i) {
			if (!in.seekg(lexical_cast<unsigned>(argv[i], 0))) {
				throw user_error("bad offset "s + argv[i]);
			}

			extract_image(in);
		}
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

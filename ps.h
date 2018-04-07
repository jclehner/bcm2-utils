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

#ifndef BCM2DUMP_PS_H
#define BCM2DUMP_PS_H
#include <cstring>
#include <string>

namespace bcm2dump {

class ps_header
{
	public:
	struct raw
	{
		uint16_t signature;
		uint16_t control;
		uint16_t ver_maj;
		uint16_t ver_min;
		uint32_t timestamp;
		uint32_t length;
		uint32_t loadaddr;
		char filename[48];
		char pad[8];
		uint32_t length1;
		uint32_t length2;
		uint16_t hcs;
		uint16_t reserved;
		uint32_t crc;
	} __attribute__((__packed__));

	static_assert(sizeof(raw) == 92, "unexpected raw header size");

	static uint16_t constexpr c_comp_none = 0;
	static uint16_t constexpr c_comp_lz = 1;
	static uint16_t constexpr c_comp_mini_lzo = 2;
	static uint16_t constexpr c_comp_reserved = 3;
	static uint16_t constexpr c_comp_nrv2d99 = 4;
	static uint16_t constexpr c_comp_lza = 5;
	static uint16_t constexpr c_dual_files = 0x100;

	ps_header() : m_valid(false)
	{
		memset(&m_raw, 0, sizeof(m_raw));
	}
	ps_header(const std::string& buf)
	{ parse(buf); }
	ps_header(const ps_header& other)
	: m_valid(other.m_valid)
	{
		memcpy(&m_raw, &other.m_raw, sizeof(m_raw));
	}

	ps_header& parse(const std::string& buf);

	bool hcs_valid() const
	{ return m_valid; }

	std::string filename() const;

	uint16_t signature() const
	{ return ntoh(m_raw.signature); }

	uint32_t length() const
	{ return ntoh(m_raw.length); }

	uint16_t control() const
	{ return ntoh(m_raw.control); }

	uint16_t compression() const
	{ return control() & 0x7; }

	bool is_dual() const
	{ return control() & c_dual_files; }

	const raw* data() const
	{ return &m_raw; }

	private:
	bool m_valid = false;
	raw m_raw;
};
}
#endif

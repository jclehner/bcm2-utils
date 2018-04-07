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

#include <cstring>
#include "util.h"
#include "ps.h"

using namespace std;

namespace bcm2dump {
ps_header& ps_header::parse(const string& buf)
{
	if (buf.size() < sizeof(m_raw)) {
		throw invalid_argument("buffer too small to contain valid header");
	}

	memcpy(&m_raw, buf.data(), sizeof(m_raw));
	uint16_t hcs = crc16_ccitt(&m_raw, sizeof(m_raw) - 8) ^ 0xffff;

	m_valid = (hcs == ntoh(m_raw.hcs));
	return *this;
}

string ps_header::filename() const
{
	return string(m_raw.filename, strnlen(m_raw.filename, sizeof(m_raw.filename)));
}
}

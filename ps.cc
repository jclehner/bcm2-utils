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

#include <arpa/inet.h>
#include <cstring>
#include "util.h"
#include "ps.h"

using namespace std;

namespace bcm2dump {
namespace {

void ntoh_header(ps_header::raw& raw)
{
#define NTOHL(x) raw.x = ntohl(raw.x)
#define NTOHS(x) raw.x = ntohs(raw.x)
	NTOHS(signature);
	NTOHS(control);
	NTOHS(ver_maj);
	NTOHS(ver_min);
	NTOHL(timestamp);
	NTOHL(length);
	NTOHL(loadaddr);
	NTOHL(length1);
	NTOHL(length2);
	NTOHS(hcs);
	NTOHS(reserved);
	NTOHL(crc);
#undef NTOHL
#undef NTOHS
}
}

ps_header& ps_header::parse(const string& buf)
{
	if (buf.size() < sizeof(m_raw)) {
		throw invalid_argument("buffer too small to contain valid header");
	}

	memcpy(&m_raw, buf.data(), sizeof(m_raw));
	uint16_t hcs = crc16_ccitt(&m_raw, sizeof(m_raw) - 8) ^ 0xffff;
	ntoh_header(m_raw);

	m_valid = (m_raw.hcs == hcs);
	//m_valid = (calc_hcs(m_raw) == m_raw.hcs);
	return *this;
}

string ps_header::filename() const
{
	return string(m_raw.filename, strnlen(m_raw.filename, sizeof(m_raw.filename)));
}

}

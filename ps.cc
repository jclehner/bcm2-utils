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

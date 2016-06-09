#include <system_error>
#include <stdexcept>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include "io.h"
using namespace std;

typedef runtime_error user_error;

namespace bcm2dump {
namespace {

unsigned to_termspeed(unsigned speed)
{
	switch (speed) {
#define CASE(n) case n: return B ## n
		CASE(230400);
		CASE(115200);
		CASE(57600);
		CASE(38400);
		//CASE(28800);
		//CASE(14400);
		CASE(9600);
		CASE(4800);
		CASE(2400);
		CASE(1200);
		CASE(300);
#undef CASE
		default:
			return 0;
	}
}

class fdio : public io
{
	public:
	fdio() : m_fd(-1) {}

	virtual ~fdio()
	{ close(); }

	virtual bool pending(unsigned timeout) const override;
	virtual void write(const string& str) override;

	protected:
	virtual void close()
	{ ::close(m_fd); }

	virtual int getc() const override;

	int m_fd;
};

class serial : public fdio
{
	public:
	serial(const char* tty, unsigned speed);
	virtual void write(const string& str) override;
	virtual void writeln(const string& str) override;
	virtual void iflush() override;
};

bool fdio::pending(unsigned timeout) const
{
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(m_fd, &fds);

	timeval tv;
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = 1000 * (timeout % 1000);

	int ret = select(m_fd + 1, &fds, NULL, NULL, &tv);
	if (ret < 0) {
		throw system_error(errno, system_category(), "select");
	}

	return ret;
}

int fdio::getc() const
{
	char c;
	ssize_t ret = read(m_fd, &c, 1);
	if (ret == 0) {
		return EOF;
	} else if (ret != 1) {
		throw system_error(errno, system_category(), "read");
	}

	return c;
}

void fdio::write(const string& str)
{
	if (::write(m_fd, str.c_str(), str.size()) != str.size()) {
		throw system_error(errno, system_category(), "write");
	}
}

void serial::write(const string& str)
{
	fdio::write(str);
	if (tcdrain(m_fd) < 0) {
		throw system_error(errno, system_category(), "tcdrain");
	}
}

void serial::writeln(const string& str)
{
	write(str + "\r\n");
	// consume the line we've just written
	readln();
}

serial::serial(const char* tty, unsigned speed)
{
	int tspeed = to_termspeed(speed);
	if (!tspeed) {
		throw user_error("invalid baud rate: " + to_string(speed));
	}

	m_fd = open(tty, O_RDWR | O_NOCTTY | O_SYNC);
	if (m_fd < 0) {
		throw system_error(errno, system_category(), string("open: ") + tty);
	}

	termios cf;
	memset(&cf, 0, sizeof(cf));
	if (tcgetattr(m_fd, &cf) != 0) {
		throw system_error(errno, system_category(), "tcgetattr");
	}

	if (cfsetispeed(&cf, tspeed) < 0 || cfsetospeed(&cf, tspeed) < 0) {
		throw system_error(errno, system_category(), "cfsetXspeed");
	}

	cf.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
	cf.c_cflag |= CS8 | CLOCAL | CREAD;
	cf.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK);
	cf.c_lflag = 0;
	cf.c_oflag = 0;
	cf.c_cc[VMIN] = 0;
	cf.c_cc[VTIME] = 5;

	if (tcsetattr(m_fd, TCSANOW, &cf) != 0) {
		throw system_error(errno, system_category(), "tcsetattr");
	}
}

void serial::iflush()
{
	if (tcflush(m_fd, TCIFLUSH) < 0) {
		throw system_error(errno, system_category(), "tcflush");
	}
}
}

string io::readln(unsigned timeout) const
{
	string line;
	bool lf = false, cr = false;

	while (pending()) {
		int c = getc();
		if (c == '\n') {
			lf = true;
			break;
		} else if (c == EOF) {
			break;
		} else if (c != '\r') {
			if (cr) {
				line.clear();
			}

			line += char(c & 0xff);
			cr = false;
		} else {
			cr = true;
		}
	}

	if (!line.empty()) {
		return line;
	}

	return lf ? string("\0", 1) : "";
}

shared_ptr<io> io::open_serial(const char* tty, unsigned speed)
{
	return make_shared<serial>(tty, speed);	
}
}

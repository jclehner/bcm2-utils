#include <system_error>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <stdexcept>
#include <termios.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <list>
#include "util.h"
#include "io.h"
using namespace std;

#define DEBUG

typedef runtime_error user_error;

namespace bcm2dump {
namespace {

list<string> lines;

void add_line(const string& line, bool in)
{
	if (lines.size() == 30) {
		lines.pop_front();
	}

	lines.push_back((in ? "==> " : "<== ") + line);

	logger::t() << lines.back() << endl;
}

bool set_nonblock(int fd, bool nonblock)
{
	int flags = 0;
	if ((flags = fcntl(fd, F_GETFL, 0)) < 0) {
		return false;
	}

	if (nonblock) {
		flags |= O_NONBLOCK;
	} else {
		flags &= ~O_NONBLOCK;
	}

	if (fcntl(fd, F_SETFL, flags) < 0) {
		return false;
	}

	return true;
}

int connect_nonblock(int fd, sockaddr* addr, socklen_t len)
{
	if (!set_nonblock(fd, true)) {
		return -1;
	}

	int err = connect(fd, addr, len);
	if (err) {
		if (errno != EINPROGRESS) {
			return -1;
		}

		fd_set rset, wset;
		FD_ZERO(&rset);
		FD_ZERO(&wset);
		FD_SET(fd, &rset);
		FD_SET(fd, &wset);

		timeval tv;
		tv.tv_sec = 5;
		tv.tv_usec = 0;

		err = select(fd + 1, &rset, &wset, NULL, &tv);
		if (err <= 0) {
			if (!err) {
				errno = ETIMEDOUT;
			}

			perror("select");

			return -1;
		}

		err = 0;
		len = sizeof(err);

		if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err) {
			if (err) {
				errno = err;
			}
			return -1;
		}
	}

	if (!set_nonblock(fd, false)) {
		return -1;
	}

	return 0;
}

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

	virtual bool pending(unsigned timeout) override;
	virtual void write(const string& str) override;

	protected:
	virtual void close()
	{ ::close(m_fd); }

	virtual int getc() override;

	int m_fd;
};

class serial : public fdio
{
	public:
	serial(const char* tty, unsigned speed);
	virtual ~serial() {}
	virtual void write(const string& str) override;
	virtual void writeln(const string& str) override;
};

class tcp : public fdio
{
	public:
	tcp(const string& addr, uint16_t port);
	virtual ~tcp() {}
	virtual void write(const string& str) override;
	virtual void writeln(const string& str) override
	{ write(str + "\r\n"); }
};

class telnet : public tcp
{
	public:
	telnet(const string& addr, uint16_t port) : tcp(addr, port) {}
	virtual ~telnet() { close(); }
	virtual void write(const string& str) override;
	virtual void writeln(const string& str) override;
	virtual int getc() override;

	protected:
	virtual void close() override;
};

bool fdio::pending(unsigned timeout)
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

int fdio::getc()
{
	char c;
	ssize_t ret = read(m_fd, &c, 1);
	if (ret == 0) {
		return eof;
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
#ifdef DEBUG
	add_line("'" + trim(str) + "'", false);
#endif
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

tcp::tcp(const string& addr, uint16_t port)
{
	m_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_fd < 0) {
		throw system_error(errno, system_category(), "socket");
	}

	addrinfo hints = { 0 };
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	addrinfo* result;
	int error = getaddrinfo(addr.c_str(), nullptr, &hints, &result);
	if (error) {
		throw system_error(error, system_category(), "getaddrinfo");
	}

	error = 0;
	for (addrinfo* rp = result; rp; rp = rp->ai_next) {
		sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(rp->ai_addr);
		addr->sin_port = htons(port);

		logger::d() << "trying " << inet_ntoa(addr->sin_addr) << endl;

		if (connect_nonblock(m_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
			error = 0;
			break;
		} else {
			error = errno;
		}
	}

	freeaddrinfo(result);

	if (error) {
		throw system_error(error, system_category(), "connect");
	}
}

void tcp::write(const string& str)
{
	if (send(m_fd, str.data(), str.size(), MSG_NOSIGNAL) != str.size()) {
		throw system_error(errno, system_category(), "send");
	}
	#ifdef DEBUG
	add_line("'" + trim(str) + "'", false);
	#endif
}

void telnet::write(const string& str)
{
	string::size_type i = str.find('\xff');
	if (i != string::npos) {
		string modstr = str;
		do {
			modstr.insert(i, 1, '\xff');
			i = str.find('\xff', i + 1);
		} while (i != string::npos);

		tcp::write(modstr);
	} else {
		tcp::write(str);
	}
}

void telnet::writeln(const string& str)
{
	tcp::writeln(str);
	readln();
}

int telnet::getc()
{
	int c = tcp::getc();
	if (c == 0xff) {
		c = tcp::getc();
		switch (c) {
		case 246:  // are you there?
			// yes i am
			write(string("\x00", 1));
			break;
		case 244:
			close();
			return eof;
		default:
			return ign;

		}

		if (c > 0 && c < 0xff) {
			logger::v() << endl << "telnet cmd " << c << endl;
			return ign;
		}
	}

	return c;
}

void telnet::close()
{
	tcp::close();
}
}

string io::readln(unsigned timeout)
{
	string line;
	bool lf = false, cr = false;

	while (pending()) {
		int c = getc();
		if (c == '\n') {
			lf = true;
			break;
		} else if (c == eof) {
			break;
		} else if (c != '\r') {
			if (cr) {
				line.clear();
			}

			if (c != ign) {
				line += char(c & 0xff);
				cr = false;
			}
		} else {
			cr = true;
		}
	}

	if (!line.empty()) {
#ifdef DEBUG
		add_line("'" + line + "'", true);
#endif
		return line;
	} else if (lf) {
#ifdef DEBUG
		add_line("(empty)", true);
#endif
	}

	return lf ? string("\0", 1) : "";
}

shared_ptr<io> io::open_serial(const char* tty, unsigned speed)
{
	return make_shared<serial>(tty, speed);	
}

shared_ptr<io> io::open_telnet(const string& address, unsigned short port)
{
	return make_shared<telnet>(address, port);
}

list<string> io::get_last_lines()
{
	return lines;
}
}

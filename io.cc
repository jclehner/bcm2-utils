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

#include <system_error>
#include <sys/types.h>
#include <stdexcept>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <list>
#include "util.h"
#include "io.h"

#ifndef _WIN32
#include <sys/socket.h>
#include <arpa/inet.h>
#include <termios.h>
#include <unistd.h>
#include <netdb.h>
#else
#define MSG_DONTWAIT 0
#include <ws2tcpip.h>
#include <io.h>
#endif

using namespace std;

#define DEBUG

typedef runtime_error user_error;

namespace bcm2dump {
namespace {

class scoped_nonblock
{
	public:
	scoped_nonblock(int fd)
	: m_fd(fd)
	{
#ifndef _WIN32
		if ((m_orig = fcntl(fd, F_GETFL, 0)) < 0) {
			throw errno_error("fcntl(F_GETFL)");
		}

		if (fcntl(fd, F_SETFL, m_orig | O_NONBLOCK) < 0) {
			throw errno_error("fcntl(F_SETFL");
		}
#else
		u_long arg = 1;
		if ((ioctlsocket(fd, FIONBIO, &arg) != 0)) {
			throw winapi_error("ioctlsocket(FIONBIO)");
		}
#endif
	}

	~scoped_nonblock()
	{
#ifndef _WIN32
		fcntl(m_fd, F_SETFL, m_orig);
#else
		u_long arg = 0;
		ioctlsocket(m_fd, FIONBIO, &arg);
#endif
	}

	private:
	int m_fd;
	int m_orig;
};

ssize_t recv_dontwait(int fd, char* buf, size_t len, int flags = 0)
{
	scoped_nonblock f(fd);
	return recv(fd, buf, len, flags);
}

ssize_t send_nosignal(int fd, const char* buf, size_t len, int flags = 0)
{
#ifdef __linux__
	flags |= MSG_NOSIGNAL;
#endif
	return send(fd, buf, len, flags);
}

int connect_nonblock(int fd, sockaddr* addr, socklen_t len)
{
#ifndef _WIN32
	scoped_nonblock f(fd);

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
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		err = select(fd + 1, &rset, &wset, NULL, &tv);
		if (err <= 0) {
			if (!err) {
				errno = ETIMEDOUT;
			}

			return -1;
		}

		err = 0;
		len = sizeof(err);

		if (getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) != 0 || err) {
			if (err) {
				errno = err;
			}
			return -1;
		}
	}

	return 0;
#else
	// FIXME
	return connect(fd, addr, len);
#endif
}

void set_port(sockaddr* sa, uint16_t port)
{
	if (sa->sa_family == AF_INET) {
		reinterpret_cast<sockaddr_in*>(sa)->sin_port = htons(port);
	} else if (sa->sa_family == AF_INET6) {
		reinterpret_cast<sockaddr_in6*>(sa)->sin6_port = htons(port);
	}
}

string addr_to_string(sockaddr* sa)
{
	char buf[INET6_ADDRSTRLEN];
	memset(buf, 0, sizeof(buf));

	if (sa->sa_family == AF_INET) {
		inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(sa)->sin_addr, buf, sizeof(buf));
	} else {
		inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(sa)->sin6_addr, buf, sizeof(buf));
	}

	return buf;
}

int to_termspeed(unsigned speed)
{
	switch (speed) {
#ifndef _WIN32
#define CASE(n) case n: return B ## n
		CASE(230400);
#else
#define CASE(n) case n: return CBR_ ## n;
#endif
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
	}
	throw user_error("invalid baud rate: " + to_string(speed));
}

class fdio : public io
{
	public:
	fdio() : m_fd(-1) {}

	virtual ~fdio()
	{ ::close(m_fd); }

	virtual bool pending(unsigned timeout) override;
	virtual void write(const string& str) override;
	virtual string read(size_t length, bool partial = true) override;

	protected:
	virtual int getc() override;
	virtual ssize_t read1(char &c);

	int m_fd;
};

#if defined(_WIN32)
class hio : public io
{
	public:
	hio() : m_h(INVALID_HANDLE_VALUE) {}

	virtual ~hio()
	{ CloseHandle(m_h); }

	//virtual bool pending(unsigned timeout) override;
	virtual void write(const string& str) override;
	virtual string read(size_t length, bool partial = true) override;

	protected:
	virtual int getc() override;

	HANDLE m_h;
};
#endif

class serial :
#ifndef _WIN32
		public fdio
#else
		public hio
#endif
{
	public:
	serial(const char* tty, unsigned speed);
	virtual ~serial() {}
	virtual void writeln(const string& str) override;
#ifndef _WIN32
	virtual void write(const string& str) override;
#else
	virtual bool pending(unsigned timeout) override;
#endif
};

class tcp : public fdio
{
	public:
	tcp(const string& addr, uint16_t port);
	virtual ~tcp() {}
	virtual void write(const string& str) override;
	virtual void writeln(const string& str) override
	{ write(str + "\r\n"); }
	virtual string read(size_t length, bool partial = true) override;

	protected:
	virtual ssize_t read1(char& c) override;
};

class telnet : public tcp
{
	public:
	telnet(const string& addr, uint16_t port) : tcp(addr, port) {}
	virtual void write(const string& str) override;
	virtual void writeln(const string& str) override;

	protected:
	virtual int getc() override;

	private:
	void handle_op_opt(int op, int opt);
	void send_op_opt(int op, int opt);

#if 0
	static int constexpr opt_binary = 0;
	static int constexpr opt_echo = 1;
	static int constexpr opt_suppres_ga = 3;
	static int constexpr opt_remote_flow_ctrl = 33;

	static int constexpr op_do = 252;
	static int constexpr op_wont = 253;
#endif
	static int constexpr op_will = 251;
	static int constexpr op_dont = 254;
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
#ifndef _WIN32
	if (ret < 0) {
		throw errno_error("select");
	}
#else
	if (ret == SOCKET_ERROR) {
		throw winsock_error("select");
	}
#endif

	return ret;
}

int fdio::getc()
{
	char c;
	ssize_t ret = read1(c);
	if (ret == 1) {
		return c & 0xff;
	} else if (!ret || errno == EWOULDBLOCK || errno == EAGAIN) {
		return eof;
	} else {
		throw errno_error("read1");
	}
}

ssize_t fdio::read1(char& c)
{
	return ::read(m_fd, &c, 1);
}

string fdio::read(size_t length, bool all)
{
	string buf(length, '\0');
	ssize_t read = ::read(m_fd, &buf[0], length);
	if (read < 0 || (all && read < length)) {
		throw errno_error("read");
	}

	buf.resize(read);
	return buf;
}

void fdio::write(const string& str)
{
	if (::write(m_fd, str.data(), str.size()) != str.size()) {
		throw errno_error("write");
	}
#ifdef DEBUG
	logger::log_io(str, false);
#endif
}

#ifndef _WIN32
void serial::write(const string& str)
{
	fdio::write(str);
	if (tcdrain(m_fd) < 0) {
		throw errno_error("tcdrain");
	}
}
#endif

void serial::writeln(const string& str)
{
	write(str + "\r");
	// consume the line we've just written
	readln();
}

#ifdef _WIN32
void hio::write(const string& str)
{
	DWORD written;
	if (!WriteFile(m_h, str.data(), str.size(), &written, nullptr) || written != str.size()) {
		throw winapi_error("WriteFile");
	}

#ifdef DEBUG
	logger::log_io(str, false);
#endif
}

string hio::read(size_t length, bool all)
{
	DWORD bytes = 0;
	auto buf = make_unique<char[]>(length);
	if (!ReadFile(m_h, buf.get(), length, &bytes, nullptr) || (all && bytes != length)) {
		throw winapi_error("ReadFile");
	}

	return string(buf.get(), bytes);
}

int hio::getc()
{
	string c = read(1, false);
	return !c.empty() ? c[0] : eof;
}

bool serial::pending(unsigned timeout)
{
	DWORD status = WaitForSingleObject(m_h, timeout);
	if (status != WAIT_FAILED) {
		return status == WAIT_OBJECT_0;
	} else {
		static bool b = false;
		if (!b) {
			logger::d("\n\nserial::pending: WaitForSingleObject failed\n\n");
			b = true;
		}
	}

	DWORD mask = 0;

	while (GetCommMask(m_h, &mask)) {
		if (mask & EV_RXCHAR) {
			return true;
		} else if (mask & EV_ERR) {
			throw runtime_error("line status error");
		} else if (timeout--) {
			Sleep(1);
		} else {
			logger::d("serial::pending: returning false!\n");
			return false;
		}
	}

	throw winapi_error("GetCommMask");
}
#endif

serial::serial(const char* tty, unsigned speed)
{
#ifndef _WIN32
	m_fd = open(tty, O_RDWR | O_NOCTTY | O_SYNC);
	if (m_fd < 0) {
		throw errno_error(string(errno != ENOENT ? "open: " : "") + tty);
	}

	termios cf;
	memset(&cf, 0, sizeof(cf));
	if (tcgetattr(m_fd, &cf) != 0) {
		throw errno_error("tcgetattr");
	}

	int tspeed = to_termspeed(speed);
	if (cfsetispeed(&cf, tspeed) < 0 || cfsetospeed(&cf, tspeed) < 0) {
		throw errno_error("cfsetXspeed");
	}

	cf.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
	cf.c_cflag |= CS8 | CLOCAL | CREAD;
	cf.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK);
	cf.c_lflag = 0;
	cf.c_oflag = 0;
	cf.c_cc[VMIN] = 0;
	cf.c_cc[VTIME] = 5;

	if (tcsetattr(m_fd, TCSANOW, &cf) != 0) {
		throw errno_error("tcsetattr");
	}
#else
	m_h = CreateFile(tty,
			GENERIC_READ | GENERIC_WRITE,
			0,
			0,
			OPEN_EXISTING,
			0,
			0);
	if (m_h == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		throw winapi_error(string(error != ERROR_FILE_NOT_FOUND ? "CreateFile: " : "") + tty);
	}

	DCB dcb = { 0 };
	dcb.DCBlength = sizeof(dcb);

	if (!GetCommState(m_h, &dcb)) {
		throw winapi_error("GetCommState");
	}

	dcb.BaudRate = to_termspeed(speed);
	dcb.ByteSize = 8;
	dcb.StopBits = ONESTOPBIT;
	dcb.Parity = NOPARITY;

	if (!SetCommState(m_h, &dcb)) {
		throw winapi_error("SetCommState");
	}

	COMMTIMEOUTS timeouts = { 0 };
	timeouts.ReadIntervalTimeout = 50;
	timeouts.ReadTotalTimeoutConstant = 50;
	timeouts.ReadTotalTimeoutMultiplier = 0;

	if (!SetCommTimeouts(m_h, &timeouts)) {
		throw winapi_error("SetCommTimeouts");
	}

	if (!SetCommMask(m_h, EV_RXCHAR | EV_ERR)) {
		throw winapi_error("SetCommMask");
	}
#endif
}

tcp::tcp(const string& addr, uint16_t port)
{
	addrinfo hints = { 0 };
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	addrinfo* result = nullptr;
	int error = getaddrinfo(addr.c_str(), nullptr, &hints, &result);
	if (error) {
		if (error == EAI_NONAME) {
			throw system_error(error, getaddrinfo_category(), addr);
		}
#ifndef _WIN32
		else if (error != EAI_SYSTEM) {
			throw system_error(error, getaddrinfo_category(), "getaddrinfo");
		} else {
			throw errno_error("getaddrinfo");
		}
#else
		else {
			throw winsock_error("getaddrinfo");
		}
#endif
	}

	for (addrinfo* rp = result; rp; rp = rp->ai_next) {
		if (rp->ai_family != AF_INET && rp->ai_family != AF_INET6) {
			continue;
		}

		m_fd = socket(rp->ai_family, SOCK_STREAM, 0);
		if (m_fd >= 0) {
			set_port(rp->ai_addr, port);
			if (connect_nonblock(m_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
				error = 0;
				break;
			} else {
				::close(m_fd);
#ifndef _WIN32
				error = errno;
#else
				error = WSAGetLastError();
#endif
				logger::v() << addr_to_string(rp->ai_addr) << ": connect:" << error << endl;
			}
		} else {
#ifndef _WIN32
			error = errno;
#else
			error = WSAGetLastError();
#endif
			logger::d() << addr_to_string(rp->ai_addr) << ": socket: " << error << endl;
		}
	}

	freeaddrinfo(result);

	if (error) {
		string fn = (m_fd >= 0 ? "connect" : "socket");
#ifndef _WIN32
		throw errno_error(fn, error);
#else
		throw winsock_error(fn, error);
#endif
	}
}

void tcp::write(const string& str)
{
	if (send_nosignal(m_fd, str.data(), str.size()) != str.size()) {
		throw errno_error("send");
	}
	#ifdef DEBUG
	logger::log_io(str, false);
	#endif
}

ssize_t tcp::read1(char& c)
{
	return recv_dontwait(m_fd, &c, 1);
}

string tcp::read(size_t length, bool all)
{
	string buf(length, '\0');
	ssize_t read = recv(m_fd, &buf[0], length, MSG_DONTWAIT);
	if (read < 0 || (all && read < length)) {
		throw errno_error("read");
	}

	buf.resize(read);
	return buf;
}

void telnet::write(const string& str)
{
	string::size_type i = str.find_first_of("\xff\r");
	if (i != string::npos) {
		string modstr = str;
		do {
			if (str[i] == '\xff') {
				modstr.insert(i, 1, '\xff');
				i += 2;
			} else if (str[i] == '\r') {
				if ((i + 1) < str.size()) {
					if (str[i + 1] != '\n') {
						modstr.insert(i + 1, 1, '\0');
						i += 2;
					}
				} else {
					modstr += '\0';
					i += 1;
				}
			}

			i = modstr.find_first_of("\xff\r", i + 1);
		} while (i != string::npos);

		tcp::write(modstr);
	} else {
		tcp::write(str);
	}
}

void telnet::writeln(const string& str)
{
	write(str + "\r");
	readln();
}

int telnet::getc()
{
	int c = tcp::getc();
	if (c == 0xff) {
		c = tcp::getc();
		if (c < 0xff) {
			int opt = tcp::getc();
			logger::d() << "telnet: received command " << c << "," << opt << endl;
			if (c >= op_will && c <= op_dont) {
				//logger::d() << "telnet: handling command " << c << "," << opt << endl;
				//handle_op_opt(c, opt);
			} else {
				//logger::d() << "telnet: not handling command " << c << "," << opt << endl;
			}
			return ign;
		}
	}

	return c;
}

// the bfc telnet server sends the following
// telnet commands when connecting:
//
//   ff fd 21 = DO,remote-flow-ctrl
//   ff fb 03 = WILL,supress-go-ahead
//   ff fb 01 = WILL,ECHO
//
// currently, we only allow supress-go-ahead
// and echo, and try to fend off everything else
//

#if 0
void telnet::handle_op_opt(int op, int opt)
{
	if (op == op_will) {
		if (opt == opt_suppres_ga || opt == opt_echo) {
			send_op_opt(op_do, opt);
		} else {
			send_op_opt(op_dont, opt);
		}
	} else if (op == op_do) {
		send_op_opt(op_wont, opt);
	} else if (opt == op_wont) {
		send_op_opt(op_dont, opt);
	} else if (opt == op_dont) {
		send_op_opt(op_wont, opt);
	}
}

void telnet::send_op_opt(int op, int opt)
{
	logger::d() << endl << "telnet: sending " << op << "," << opt << endl;
	tcp::write(string("\xff") + char(op) + char(opt));
}
#endif
}

string io::readln(unsigned timeout)
{
	string line;
	bool lf = false, cr = false;

	while (pending(timeout)) {
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
		logger::log_io(line, true);
#endif
		return line;
	} else if (lf) {
#ifdef DEBUG
		logger::log_io("", true);
#endif
	}

	return lf ? string("\0", 1) : "";
}

shared_ptr<io> io::open_telnet(const string& address, unsigned short port)
{
	return make_shared<telnet>(address, port);
}

shared_ptr<io> io::open_tcp(const string& address, unsigned short port)
{
	return make_shared<tcp>(address, port);
}

shared_ptr<io> io::open_serial(const char* tty, unsigned speed)
{
	return make_shared<serial>(tty, speed);
}
}

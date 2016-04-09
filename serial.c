/**
 * bcm2-utils
 * Copyright (C) 2016 Joseph C. Lehner <joseph.c.lehner@gmail.com>
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

#include <termios.h>
#include "bcm2dump.h"

bool ser_debug = false;

static int to_termspeed(unsigned speed)
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

int ser_open(const char *dev, unsigned speed)
{
	speed = to_termspeed(speed);
	if (!speed) {
		fprintf(stderr, "error: invalid baud rate %u\n", speed);
		return -1;
	}

	int fd = open(dev, O_RDWR | O_NOCTTY);
	if (fd == -1) {
		perror(dev);
		return -1;
	}

	struct termios c;
	memset(&c, 0, sizeof(c));

	c.c_cflag = CS8 | CLOCAL | CREAD;
	c.c_iflag = IGNPAR;
	c.c_oflag = 0;
	c.c_lflag = ICANON;

	c.c_cc[VMIN] = 0;
	c.c_cc[VTIME] = 0;

	if (cfsetispeed(&c, speed) < 0 || cfsetospeed(&c, speed) < 0) {
		perror("error: cfsetispeed/cfsetospeed");
		close(fd);
		return -1;
	}

	if (tcsetattr(fd, TCSAFLUSH, &c) < 0) {
		perror("error: tcsetattr");
		close(fd);
		return -1;
	}

	return fd;
}

bool ser_write(int fd, const char *buf)
{
	if (ser_debug) {
		fprintf(stderr, "<< %s", buf);
	}

	size_t len = strlen(buf);
	ssize_t written = write(fd, buf, strlen(buf));
	if (written < 0 || written != len) {
		perror("error: write");
		return false;
	} else if (tcdrain(fd) < 0) {
		perror("error: tcdrain");
		return false;
	}

	return true;
}

bool ser_iflush(int fd)
{
	if (tcflush(fd, TCIFLUSH) < 0) {
		perror("error: tcflush");
		return false;
	}

	return true;
}

bool ser_read(int fd, char *buf, size_t len)
{
	if (ser_debug) {
		fprintf(stderr, ">> ");
		fflush(stderr);
	}

	size_t bytes = read(fd, buf, len - 1);
	if (bytes < 0) {
		perror("error: read");
		return false;
	}

	buf[bytes] = '\0';

	char *p = buf;
	for (; *p == '\r' || *p == '\n'; ++p)
		;

	if (p > buf) {
		bytes -= (p - buf);
		memmove(buf, p, bytes);
	}

	p += bytes - 1;

	for (; *p == '\r' || *p == '\n'; --p)
		*p = '\0';

	if (ser_debug) {
		fprintf(stderr, "%s\n", buf);
	}

	return true;
}

int ser_select(int fd, unsigned msec)
{
	fd_set fds;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	struct timeval tv;
	tv.tv_sec = msec / 1000;
	tv.tv_usec = 1000 * (msec % 1000);

	int status = select(fd + 1, &fds, NULL, NULL, &tv);
	if (status < 0) {
		perror("error: select");
	}

	return status;
}

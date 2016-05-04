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

#include <arpa/inet.h>
#include "bcm2dump.h"

static bool expect(int fd, const char *str)
{
	char line[256];

	if (ser_read(fd, line, sizeof(line))) {
		if (strstr(line, str)) {
			return true;
		}

		fprintf(stderr, "error: Expected '%s', got '%s'\n", str, line);
	}

	return false;
}

static bool menu_select(int fd, const char *opt)
{
	return bl_menu_wait(fd, true, false) && ser_write(fd, opt);
}

bool bl_readw_begin(int fd)
{
	return menu_select(fd, "r");
}

bool bl_readw_end(int fd)
{
	return ser_write(fd, "\r\n");
}

bool bl_readw(int fd, unsigned addr, char *val)
{
	char line[256];

	sprintf(line, "%08x\r\n", addr);
	if (!ser_iflush(fd) || !ser_write(fd, line) || !expect(fd, "address:")) {
		return false;
	}

	line[0] = '\0';
	int ready;

	while ((ready = ser_select(fd, 100)) > 0) {
		if (!ser_read(fd, line, sizeof(line))) {
			return false;
		}

		if (strstr(line, "Value at")) {
			break;
		}
	}

	if (ready < 0) {
		return false;
	}

	unsigned addr2;
	uint32_t val2;

	if (sscanf(line, "Value at %x: %" SCNx32 " (hex)", &addr2, &val2) != 2) {
		fprintf(stderr, "error: unexpected response '%s'\n", line);
		return false;
	}

	if (addr2 != addr) {
		fprintf(stderr, "error: expected addr 0x%08x, got 0x%08x\n", addr, addr2);
		return false;
	}

	val2 = htonl(val2);
	memcpy(val, &val2, 4);

	return ser_read(fd, line, sizeof(line));
}

bool bl_read(int fd, unsigned addr, void *buf, size_t len)
{
	char *p = buf;

	if (bl_readw_begin(fd)) {
		size_t i = 0;
		for (; i < len; i += 4) {
			if (!bl_readw(fd, addr + i, p + i)) {
				break;
			}
		}

		if (bl_readw_end(fd)) {
			return i == len;
		}
	}

	return false;
}

bool bl_writew(int fd, unsigned addr, const char *word)
{
	if (!menu_select(fd, "w")) {
		return false;
	}

	char line[256];
	sprintf(line, "%08x\r\n", addr);
	if (!ser_write(fd, line) || !expect(fd, "address:")) {
		return false;
	}

	sprintf(line, "%02x%02x%02x%02x\r\n", word[0] & 0xff, word[1] & 0xff, 
			word[2] & 0xff, word[3] & 0xff);
	if (!ser_write(fd, line) || !expect(fd, "value:")) {
		return false;
	}

	return bl_menu_wait(fd, false, false);
}

bool bl_write(int fd, unsigned addr, const void *buf, size_t len)
{
	const char *p = buf;
	size_t i = 0;
	for (; i < len; i += 4) {
		if (!bl_writew(fd, addr + i, p + i)) {
			return false;
		}
	}

	return true;
}

bool bl_jump(int fd, unsigned addr)
{
	if (!menu_select(fd, "j")) {
		return false;
	}

	char line[256];
	sprintf(line, "%08x\r\n", addr);
	if (!ser_write(fd, line) || !expect(fd, "Jump to")) {
		return false;
	}

	return true;
}

bool bl_menu_wait(int fd, bool write, bool quiet)
{
	if (write) {
		if (!ser_iflush(fd) || !ser_write(fd, "\r\n")) {
			return false;
		}
	}

	bool ok = false;
	char line[256];
	int ready;

	while ((ready = ser_select(fd, 100)) > 0) {
		if (!ser_read(fd, line, sizeof(line))) {
			return false;
		}

		if (strstr(line, "Main Menu:")) {
			ok = true;
		}
	}

	if (ready < 0 || !ser_iflush(fd)) {
		return false;
	} else if (!ok) {
		if (!quiet) {
			fprintf(stderr, "error: not in main menu.\n");
		}
		return false;
	}

	return true;
}

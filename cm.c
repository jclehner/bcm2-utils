#include "bcm2dump.h"

static bool consume_lines(int fd)
{
	int pending;
	char line[256];

	while ((pending = ser_select(fd, 100)) > 0) {
		if (!ser_read(fd, line, sizeof(line))) {
			return false;
		}
	}

	if (pending < 0) {
		return false;
	}

	return ser_iflush(fd);
}

bool cm_flash_open(int fd, const char *part)
{
	char line[256];

	if (!cm_flash_close(fd)) {
		return false;
	}

	sprintf(line, "/flash/open %s\r\n", part);
	return ser_iflush(fd) && ser_write(fd, line) && consume_lines(fd);
}

bool cm_flash_close(int fd)
{
	return ser_write(fd, "/flash/close\r\n") && consume_lines(fd);
}

bool cm_flash_read(int fd, unsigned addr, void *buf, size_t len)
{
	if (len % 16) {
		fprintf(stderr, "error: length must be multiple of 16\n");
		return false;
	}

	char line[256];

	sprintf(line, "/flash/readDirect %zu %u\r\n", len, addr);
	if (!ser_iflush(fd) || !ser_write(fd, line)) {
		return false;
	}

	bool data = false;

	while (len && ser_select(fd, 1000) > 0 && ser_read(fd, line, sizeof(line))) {
		if (!line[0] || line[0] == ' ') {
			continue;
		}

		if (cm_flash_parse_values(line, buf, !data)) {
			data = true;
			buf = ((char*)buf) + 16;
			len -= 16;
		} else if (data) {
			// a line after the first hexdump line failed to parse
			return false;
		}
	}

	if (!data) {
		fprintf(stderr, "error: no data\n");
	}

	if (!ser_iflush(fd)) {
		return false;
	}

	return !len;
}

bool cm_flash_parse_values(const char *line, char *buf16, bool quiet)
{
	unsigned i, data[16];

	int s = sscanf(line, "%x %x %x %x   %x %x %x %x   %x %x %x %x   %x %x %x %x",
			data + 0, data + 1, data + 2, data + 3, data + 4, data + 5,
			data + 6, data + 7, data + 8, data + 9, data + 10, data + 11,
			data + 12, data + 13, data + 14, data + 15);

	if (s != 16) {
		if (!quiet) {
			fprintf(stderr, "\nerror: invalid line '%s'\n", line);
		}
		return false;
	}

	for (i = 0; i < 16; ++i) {
		buf16[i] = data[i] & 0xff;
	}

	return true;
}

bool cm_mem_read(int fd, unsigned addr, void *buf, size_t len)
{
	if (len % 16) {
		fprintf(stderr, "error: length must be multiple of 16\n");
		return false;
	}

	char line[256];
	sprintf(line, "/read_memory -s 1 -n %zu 0x%x\r\n", len, addr);

	if (!ser_iflush(fd) || !ser_write(fd, line)) {
		return false;
	}

	// read the line we just wrote
	if (!ser_read(fd, line, sizeof(line))) {
		return false;
	}

	unsigned end = addr + len;
	while (addr < end) {
		if (!ser_read(fd, line, sizeof(line))) {
			return false;
		}

		if (!*line || !strchr(line, ':')) {
			continue;
		}

		if (!cm_mem_parse_values(line, buf)) {
			return false;
		}

		buf = ((char*)buf) + 16;
		addr += 16;
	}

	return ser_iflush(fd);
}

bool cm_mem_parse_values(const char *line, char *buf16)
{
	unsigned i, off, data[16];

	int s = sscanf(line, "%x: %x %x %x %x  %x %x %x %x  %x %x %x %x  %x %x %x %x ",
			&off, data + 0, data + 1, data + 2, data + 3, data + 4, data + 5,
			data + 6, data + 7, data + 8, data + 9, data + 10, data + 11,
			data + 12, data + 13, data + 14, data + 15);

	if (s != 17) {
		fprintf(stderr, "\nerror: invalid line '%s'\n", line);
		return false;
	}

	for (i = 0; i < 16; ++i) {
		buf16[i] = data[i] & 0xff;
	}

	return true;
}

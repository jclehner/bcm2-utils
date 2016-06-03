#include "bcm2dump.h"

bool cm_flash_read(int fd, const char *part, unsigned addr, void *buf, size_t len)
{
	if (len % 16) {
		fprintf(stderr, "error: length must be multiple of 16\n");
		return false;
	}

	bool ok = false;
	char line[256];

	sprintf(line, "/flash/open %s\r\n", part);
	if (!ser_iflush(fd) || !ser_write(fd, line)) {
		goto out;
	}

	sprintf(line, "/flash/read 4 %zu %u\r\n", len, addr);
	if (!ser_iflush(fd) || !ser_write(fd, line)) {
		goto out;
	}

out:
	sprintf(line, "/flash/close\r\n");
	if (!ser_write(fd, line) || !ser_iflush(fd)) {
		ok = false;
	}

	return ok;
}

bool cm_read(int fd, unsigned addr, void *buf, size_t len)
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

		if (!cm_parse_values(line, buf)) {
			return false;
		}

		buf = ((char*)buf) + 16;
		addr += 16;
	}

	return ser_iflush(fd);
}

bool cm_parse_values(const char *line, char *buf16)
{
	unsigned i, off, data[16];

	int s = sscanf(line, "%x: %x %x %x %x  %x %x %x %x  %x %x %x %x  %x %x %x %x ",
			&off, data + 0, data + 1, data + 2, data + 3, data + 4, data + 5,
			data + 6, data + 7, data + 8, data + 9, data + 10, data + 11,
			data + 12, data + 13, data + 14, data + 15);

	if (s != 17) {
		fprintf(stderr, "\n****");
		fprintf(stderr, "\nerror: invalid line '%s'\n", line);
		return false;
	}

	for (i = 0; i < 16; ++i) {
		buf16[i] = data[i] & 0xff;
	}

	return true;
}

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

#ifndef BCM2_DUMP_H
#define BCM2_DUMP_H
#include <arpa/inet.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include "profile.h"

// pointer to buffer, offset, length
#define CODE_DUMP_PARAMS_PBOL 0
// buffer, offset, length
#define CODE_DUMP_PARAMS_BOL (1 << 0)
// offset, buffer, length
#define CODE_DUMP_PARAMS_OBL (1 << 1)

int ser_open(const char *dev, unsigned speed);
bool ser_write(int fd, const char *str);
bool ser_read(int fd, char *buf, size_t len);
bool ser_iflush(int fd);
int ser_select(int fd, unsigned msec);

extern bool ser_debug;

bool bl_readw_begin(int fd);
bool bl_readw_end(int fd);
bool bl_readw(int fd, unsigned addr, char *word);
bool bl_read(int fd, unsigned addr, void *buf, size_t len);
bool bl_writew(int fd, unsigned addr, const char *word);
bool bl_write(int fd, unsigned addr, const void *buf, size_t len);
bool bl_jump(int fd, unsigned addr);
bool bl_menu_wait(int fd, bool write, bool quiet);

bool cm_read(int fd, unsigned addr, void *buf, size_t len);
bool cm_parse_values(const char *line, char *buf16);

struct progress {
	unsigned min;
	unsigned max;
	time_t beg;
	time_t last;
	unsigned cur;
	unsigned tmp;
	unsigned speed_now;
	unsigned speed_avg;
	float percentage;
	struct tm eta;
	unsigned eta_days;
};

void progress_init(struct progress *p, unsigned min, unsigned len);
void progress_add(struct progress *p, unsigned n);
void progress_print(struct progress *p, FILE *fp);

struct code_cfg {
	struct bcm2_profile *profile;
	struct bcm2_addrspace *addrspace;
	uint32_t buffer;
	uint32_t offset;
	uint32_t length;
	uint32_t chunklen;
	uint32_t entry;
	uint32_t *code;
	uint32_t codesize;
	bool nopatch;
};

typedef void (*code_upload_callback)(struct progress*, bool, void*);

bool code_init_and_upload(int fd, struct code_cfg *cfg, code_upload_callback callback, void *arg);
bool code_run(int fd, struct code_cfg *cfg);
bool code_parse_values(const char *line, uint32_t *val, bool *parseable);

#endif


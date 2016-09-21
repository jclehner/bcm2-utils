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

#ifndef BCM2UTILS_PROGRESS_H
#define BCM2UTILS_PROGRESS_H
#include <stdio.h>
#include <time.h>
#include "profile.h"

// pointer to buffer, offset, length
#define CODE_DUMP_PARAMS_PBOL 0
// buffer, offset, length
#define CODE_DUMP_PARAMS_BOL (1 << 0)
// offset, buffer, length
#define CODE_DUMP_PARAMS_OBL (1 << 1)

#ifdef __cplusplus
extern "C" {
#endif

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
void progress_set(struct progress *p, unsigned n);
void progress_print(struct progress *p, FILE *fp);

#ifdef __cplusplus
}
#endif

#endif


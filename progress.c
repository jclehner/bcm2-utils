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

#include <time.h>
#include "progress.h"

static void gmtime_days(time_t time, unsigned *days, struct tm *tm)
{
	*days = time / 86400;
	if (*days) {
		time -= *days * 86400;
	}

#ifndef _WIN32
	gmtime_r(&time, tm);
#else
	memcpy(tm, gmtime(&time), sizeof(struct tm));
#endif
}

static void print_time(FILE *fp, unsigned days, struct tm *tm)
{
	if (days) {
		fprintf(fp, "%2dd ", days);
	} else {
		fprintf(fp, "    ");
	}

	char buf[128];
	if (tm->tm_year != 0xffff && strftime(buf, sizeof(buf) - 1, "%H:%M:%S", tm) > 0) {
		fprintf(fp, "%s", buf);
	} else {
		fprintf(fp, "--:--:--");
	}
}

void progress_init(struct progress *p, unsigned min, unsigned len)
{
	memset(p, 0, sizeof(*p));

	p->speed_now = p->speed_avg = 0;
	p->beg = p->last = time(NULL);
	p->percentage = 0.0;
	p->tmp = 0;
	p->cur = min;
	p->min = min;
	p->max = len ? (min - 1 + len) : min;
}

void progress_set(struct progress *p, unsigned n)
{
	progress_add(p, n - p->cur);
}

void progress_add(struct progress *p, unsigned n)
{
	if (!n) {
		return;
	}

	p->tmp += n;
	p->cur += n;

	if (p->max && (p->cur > p->max)) {
		p->cur = p->max;
	} else if (p->cur < p->min) {
		p->cur = p->min;
	}

	time_t now = time(NULL);
	if (now > p->last) {
		p->speed_now = p->tmp / (now - p->last);
		if (now > p->beg) {
			p->speed_avg = (p->cur - p->min) / (now - p->beg);
		} else {
			p->speed_avg = p->speed_now;
		}

		if (p->speed_avg) {
			gmtime_days((p->max - p->cur) / p->speed_avg, &p->eta_days, &p->eta);
		} else {
			p->eta.tm_year = 0xffff;
		}

		p->last = now;
		p->tmp = 0;
	}

	if (p->max) {
		float cur = p->cur - p->min;
		float max = p->max - p->min;

		p->percentage = 100.0f * cur / max;
	} else {
		p->percentage = -1.0f;
	}
}

void progress_print(struct progress *p, FILE *fp)
{
	if (p->percentage >= 0.0f) {
		fprintf(fp, "%6.2f%% (0x%08x) ", p->percentage, p->cur);
	} else {
		fprintf(fp, "---.--%% (0x%08x) ", p->cur);
	}

	if (p->cur < p->max) {
		fprintf(fp, "%5u|%5u bytes/s (ETA  ", p->speed_now, p->speed_avg);
		print_time(fp, p->eta_days, &p->eta);
		fprintf(fp, ")");
		fflush(fp);
	} else {
		struct tm elapsed;
		unsigned days;
		time_t diff = time(NULL) - p->beg;

		gmtime_days(diff, &days, &elapsed);

		long speed = (p->max - p->min) / (diff ? diff : 1);
		fprintf(fp, "      %5ld bytes/s (ELT  ", speed ? speed : p->speed_now);
		print_time(fp, days, &elapsed);
		fprintf(fp, ")\n");
	}
}





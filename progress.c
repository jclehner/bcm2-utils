#include "bcm2dump.h"

static void gmtime_days(time_t time, unsigned *days, struct tm *tm)
{
	*days = time / 86400;
	if (*days) {
		time -= *days * 86400;
	}

	gmtime_r(&time, tm);
}

static void print_time(FILE *fp, unsigned days, struct tm *tm)
{
	if (days) {
		fprintf(fp, "%2dd ", days);
	} else {
		fprintf(fp, "    ");
	}

	char buf[128];
	if (tm->tm_year != 0xffff && strftime(buf, sizeof(buf) - 1, "%T", tm) > 0) {
		fprintf(fp, "%s", buf);
	} else {
		fprintf(fp, "--:--:--");
	}
}

void progress_init(struct progress *p, unsigned min, unsigned len)
{
	memset(p, 0, sizeof(*p));

	p->beg = p->last = time(NULL);
	p->percentage = 0.0;
	p->cur = min;
	p->min = min;
	p->max = min - 1 + len;
}

void progress_add(struct progress *p, unsigned n)
{
	if (!n) {
		return;
	}

	p->tmp += n;
	p->cur += n;

	if (p->cur > p->max) {
		p->cur = p->max;
	} else if (p->cur < p->min) {
		p->cur = p->min;
	}

	time_t now = time(NULL);
	if (now > p->last) {
		p->speed_now = p->tmp / (now - p->last);
		p->speed_avg = (p->cur - p->min) / (now - p->beg);

		if (p->speed_avg) {
			gmtime_days((p->max - p->cur) / p->speed_avg, &p->eta_days, &p->eta);
		} else {
			p->eta.tm_year = 0xffff;
		}

		p->last = now;
		p->tmp = 0;
	}

	float cur = p->cur - p->min;
	float max = p->max - p->min;

	p->percentage = 100.0f * cur / max;
}

void progress_print(struct progress *p, FILE *fp)
{
#if 0
	unsigned speed = p->cur < p->max ? p->speed_now : p->speed_avg;
	fprintf(fp, "%6.2f%% (0x%08x) %4d bytes/s ", p->percentage, p->cur, speed);
#else
	fprintf(fp, "%6.2f%% (0x%08x) %4d|%4d bytes/s ", p->percentage, p->cur, p->speed_now, p->speed_avg);
#endif

	if (p->cur < p->max) {
		fprintf(fp, "(ETA  ");
		print_time(fp, p->eta_days, &p->eta);
	} else {
		struct tm elapsed;
		unsigned days;

		gmtime_days(time(NULL) - p->beg, &days, &elapsed);
		fprintf(fp, "(ELT  ");
		print_time(fp, days, &elapsed);
	}

	fprintf(fp, ")");
	fflush(fp);
}





#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "common.h"

static char *pretty_num(uint32_t n)
{
	static char buf[32];

	if (!(n % (1024 * 1024))) {
		sprintf(buf, "%u M", n / (1024 * 1024));
	} else if (!(n % 1024)) {
		sprintf(buf, "%u K", n / 1024);
	} else {
		sprintf(buf, "%u", n);
	}

	return buf;
}

static struct bcm2_profile *find_profile(const char *name)
{
	struct bcm2_profile *profile = bcm2_profile_find(name);
	if (!profile) {
		fprintf(stderr, "error: profile '%s' not found\n", name);
	}

	return profile;
}


bool bcm2_profile_override(struct bcm2_profile *profile, const char *arg)
{
	char *value = strchr(arg, '=');	
	if (value) {
		*value = '\0';
		++value;

#define HANDLE_PROF_OVERRIDE_NUM(x) \
		do { if (!strcmp(arg, #x)) { if (strtou(value, &profile->x, true)) return true; } } while(0)

		HANDLE_PROF_OVERRIDE_NUM(baudrate);
		HANDLE_PROF_OVERRIDE_NUM(loadaddr);
		HANDLE_PROF_OVERRIDE_NUM(buffer);
		HANDLE_PROF_OVERRIDE_NUM(kseg1mask);
		HANDLE_PROF_OVERRIDE_NUM(printf);
#undef HANDLE_PROF_OVERRIDE_NUM
	}

	fprintf(stderr, "error: invalid profile override '%s=%s'\n", arg, value);
	return false;
}

static void list_profile_and_exit(const char *name, int verbosity)
{
	struct bcm2_profile *profile;

	if (name) {
		profile = find_profile(name);
		if (!profile) {
			exit(1);
		}

		printf("PROFILE '%s': %s\n", profile->name, profile->pretty);
		printf("======================================================\n");
#define DUMP(o, x, fmt) printf("%-10s  " fmt "\n", #x, o->x)
		DUMP(profile, baudrate, "%u");
		DUMP(profile, pssig, "0x%04x");
		printf("%-10s  %s\n", "endianness", profile->mipsel ? "little" : "big");
		DUMP(profile, cfg_md5key, "%s");

		unsigned i = 0;
		for (; profile->cfg_defkeys[i][0]; ++i) {
			printf("-%10s  %s\n", i ? "" : "cfg_defkeys", profile->cfg_defkeys[i]);
		}

		printf("\n");
		if (verbosity) {
			DUMP(profile, loadaddr, "0x%08x");
			DUMP(profile, buffer, "0x%08x");
			DUMP(profile, buflen, "%u");
			DUMP(profile, kseg1mask, "0x%08x");
			DUMP(profile, printf, "0x%08x");
			DUMP(profile, scanf, "0x%08x");
			printf("%-10s 0x%08x '%s'\n", "magic", profile->magic.addr, profile->magic.data);
			printf("\n");
		}
#undef DUMP

		struct bcm2_addrspace *space = profile->spaces;

		if (!space->name[0]) {
			printf("(no address spaces defined)\n");
		} else {
			for (; space->name[0]; ++space) {
				printf("SPACE '%s': 0x%08x-", space->name, space->min);
				if (space->size) {
					printf("0x%08x (%s) ", space->min + space->size, 
							pretty_num(space->size));
				} else {
					printf("? ");
				}

				if (!strcmp(space->name, "ram")) {
					printf("RW");
				} else {
					printf("%c", space->ram || space->read.addr ? 'R' : ' ');
					printf("%c", space->write.addr ? 'W' : ' ');
				}

				if (strcmp(space->name, "ram") && space->ram) {
					printf(" (ram)");
				}

				printf("\n");
				printf("name------------------offset--------size--------------\n");
				
				struct bcm2_partition *part = space->parts;
				if (!part->name[0]) {
					printf("(no partitions defined)\n");
					continue;
				}

				for (; part->name[0]; ++part) {
					printf("%-16s  0x%08x  0x%08x  (%ss)\n", part->name, part->offset, part->size, 
							pretty_num(part->size));
				}

				if (verbosity) {
					printf("\n");
					struct bcm2_func *funcs[] = { &space->read, &space->write };
					unsigned i = 0;
					for (; i < 2; ++i) {
						if (!funcs[i]->addr) {
							continue;
						}

						printf("%s: 0x%08x, mode 0x%02x\n",
								i ? "write" : "read ", funcs[i]->addr,
								funcs[i]->mode);

						unsigned k = 0;
						for (; funcs[i]->patch[k].addr && k < BCM2_PATCH_NUM; ++k) {
							printf("patch%u: 0x%08x -> %08x\n", k,
									funcs[i]->patch[k].addr, funcs[i]->patch[k].word);
						}
					}
				}

				printf("\n");
			}
		}
	} else {
		profile = bcm2_profiles;
		for (; profile->name[0]; ++profile) {
			printf("%-16s  %s\n", profile->name, profile->pretty);
		}
	}

	exit(0);
}


bool strtou(const char *str, unsigned *n, bool quiet)
{
	int base = !strncmp(str, "0x", 2) ? 16 : 10;
	char *end = NULL;
	unsigned long val = strtoul(str, &end, base);

	if (base == 10 && *end) {
		switch (*end) {
			case 'M':
			case 'm':
				val *= 1024;
				// fall through
			case 'K':
			case 'k':
				val *= 1024;
				++end;
				break;

			default:
				break;
		}
	}

	if (*end == '+' || *end == '-') {
		unsigned off;
		if (!strtou(end + 1, &off, true)) {
			if (!quiet) {
				fprintf(stderr, "error: invalid offset specification: %s\n", end);
			}
			return false;
		}

		if (*end == '+') {
			val += off;
		} else {
			val -= off;
		}
	} else if (*end) {
		if (!quiet) {
			fprintf(stderr, "error: invalid %s number: %s\n", base == 16 ? "hex" : "dec", str);
		}
		return false;
	}

	*n = val;

	return true;
}

bool handle_common_opt(int opt, char *arg, int *verbosity, struct bcm2_profile **profile)
{
	static bool override = false, have_profile = false;

	switch (opt) {
		case 'v':
			*verbosity += 1;
			break;
		case 'L':
			list_profile_and_exit(have_profile ? (*profile)->name : NULL, *verbosity);
			break;
		case 'O':
			if (!bcm2_profile_override(*profile, arg)) {
				return false;
			}
			override = true;
			break;
		case 'P':
			if (override) {
				fprintf(stderr, "error: must specify -P before -O\n");
				return false;
			}
			*profile = find_profile(arg);
			if (!*profile) {
				return false;
			}
			have_profile = true;
			break;
		default:
			return false;
	}

	return true;
}

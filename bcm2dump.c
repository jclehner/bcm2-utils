#include <getopt.h>
#include "bcm2dump.h"
#include "profile.h"
#include "mipsasm.h"
#include "common.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static struct bcm2_profile *profile = NULL;
static struct bcm2_addrspace *space = NULL;
const char *opt_space = NULL;
static const char *opt_off = NULL;
static const char *opt_len = NULL;
static bool opt_force = false;
static bool opt_append = false;
static bool opt_slow = false;
static bool opt_shell = false;
static int opt_verbosity = 0;
static bool auto_detect_profile = true;

static const char *opt_codefile = NULL;
static const char *opt_filename = NULL;
static const char *opt_ttydev = NULL;

static bool detect_profile(int fd)
{
	char buffer[128];
	bool is_shell = opt_shell;

	if (!is_shell && !bl_menu_wait(fd, true, true)) {
		// not in bootloader; try CM shell
		if (!ser_iflush(fd) || !ser_write(fd, "\r\n") || !ser_read(fd, buffer, sizeof(buffer))) {
			return false;
		}

		if (strncmp(buffer, "CM>", 3)) {
			fprintf(stderr, "error: neither in bootloader nor in CM shell\n");
			return false;
		}

		is_shell = true;
	}

	profile = bcm2_profiles;
	for (; profile->name[0]; ++profile) {
		if (!profile->magic.addr) {
			continue;
		}

		bool ok;

		if (!is_shell) {
			ok = bl_read(fd, profile->magic.addr, buffer, sizeof(buffer));
		} else {
			ok = cm_read(fd, profile->magic.addr, buffer, sizeof(buffer));
		}

		if (!ok) {
			return false;
		}

		size_t len = strlen(profile->magic.data);
		if (!memcmp(profile->magic.data, buffer, MIN(len, sizeof(buffer)))) {
			break;
		}
	}

	if (!profile->name[0]) {
		profile = NULL;
	} else {
		opt_shell = is_shell;
	}

	return true;
}

static bool dump_shell(int fd, unsigned offset, unsigned length, FILE *fp)
{
	char line[256];

	if (length % 16) {
		fprintf(stderr, "error: length must be a multiple of 16\n");
		return false;
	}

	if (!ser_iflush(fd) || !ser_write(fd, "\r\n") || !ser_read(fd, line, sizeof(line))) {
		return false;
	}

	if (!auto_detect_profile && strncmp(line, "CM>", 3)) {
		fprintf(stderr, "error: not in CM shell root directory\n");
		return false;
	}

	char chunk[1024];
	unsigned end = offset + length;

	struct progress pg;
	progress_init(&pg, offset, length);

	bool ok = false;

	for (; offset < end; offset += sizeof(chunk)) {
		if (!cm_read(fd, offset, chunk, MIN(sizeof(chunk), end - offset))) {
			goto out;
		}

		printf("\rdump: ");
		progress_add(&pg, sizeof(chunk));
		progress_print(&pg, stdout);

		if (fwrite(chunk, sizeof(chunk), 1, fp) != 1) {
			perror("error: fwrite");
			goto out;
		}
	}

	ok = true;
out:
	printf("\n");
	fflush(fp);
	return ok;
}

static bool dump_slow(int fd, unsigned offset, unsigned length, FILE *fp)
{
	bool ret = false;

	if (bl_readw_begin(fd)) {
		struct progress pg;
		progress_init(&pg, offset, length);

		char word[4];
		unsigned max = offset + length;

		for (; offset < max && bl_readw(fd, offset, word); offset += 4) {
			printf("\rdump: ");
			progress_add(&pg, 4);
			progress_print(&pg, stdout);

			if (fwrite(word, 4, 1, fp) != 1) {
				perror("fwrite");
				break;
			}
		}

		if (offset >= max) {
			printf("\n");
			ret = true;
		}

		if (!bl_readw_end(fd)) {
			ret = false;
		}
	}

	return ret;
}

struct callback_arg
{
	struct code_cfg *cfg;
	const char *cmd;
};

static void upload_callback(struct progress *pg, bool full, void *argp)
{
	static bool first = true;
	struct callback_arg *arg = argp;

	if (first) {
		if (full && !strcmp(arg->cmd, "dump")) {
			printf("%s: writing dump code (%u b) to ram at 0x%08x\n",
					arg->cmd, arg->cfg->codesize,
					arg->cfg->profile->loadaddr);
		}
		first = false;
	}

	if (full) {
		printf("\r%s: ", arg->cmd);
		progress_print(pg, stdout);
	} else {
		printf("\r%s: updating dump code ", arg->cmd);
		printf("%.0f%%", pg->percentage);
		fflush(stdout);
	}
}

// this function is ugly, since it handles the dump, write and exec commands
// by abusing code_init_and_upload as a generic upload method. when not
// called with cmd == "dump", the fp argument can (and should) be NULL.
static bool dump_write_exec(int fd, const char *cmd, uint32_t offset, uint32_t length, FILE *fp)
{
	struct code_cfg cfg = {
		.offset = offset,
		.length = length,
		.chunklen = 0x4000,
		.profile = profile,
		.addrspace = space,
		.code = NULL,
		.nopatch = false,
	};

	bool dump = !strcmp(cmd, "dump");
	bool verbose = false, success = false;

	const char *codefile;
	if (dump) {
		codefile = opt_codefile;
	} else {
		if (!space->mem) {
			fprintf(stderr, "error: command '%s' undefined for address space '%s'\n",
					cmd, space->name);
			return false;
		}

		codefile = opt_filename;
		if (!codefile) {
			fprintf(stderr, "error: no input file specified for '%s' command\n", cmd);
			return false;
		}

		profile->loadaddr = offset;
	}

	if (!codefile) {
		if (!space->mem && !space->read.addr) {
			fprintf(stderr, "error: no read function defined for address space '%s'\n", space->name);
			return false;
		}

		if (!profile->loadaddr || !profile->buffer || !profile->printf) {
			if (space->mem) {
				printf("dump: falling back to slow dump method\n");
				return dump_slow(fd, offset, length, fp);
			}

			fprintf(stderr, "error: profile is incomplete; cannot dump non-ram address space '%s'\n",
					space->name);
			return false;
		}

		if (profile->loadaddr & 0xffff) {
			fprintf(stderr, "error: load address 0x%08x is not aligned on a 64k boundary\n",
					profile->loadaddr);
			return false;
		}
	} else {
		FILE *cf = fopen(codefile, "r");
		if (!cf) {
			perror(codefile);
			return false;
		}

		fseek(cf, 0, SEEK_END);
		long s = ftell(cf);
		if (s < 0) {
			perror("error: ftell");
		}

		if (!s) {
			fprintf(stderr, "error: file '%s' is empty\n", codefile);
			return false;
		}

		cfg.codesize = s;
		cfg.code = malloc(cfg.codesize);

		if (!cfg.code) {
			perror("error: malloc");
			return false;
		}

		rewind(cf);

		if (fread(cfg.code, cfg.codesize, 1, cf) != 1) {
			perror("error: fread");
			goto out;
		}

		fclose(cf);
	}

	unsigned max = offset + length;
	printf("%s: %s 0x%08x-0x%08x\n", cmd, space->name, offset, max - 1);

	struct callback_arg arg = { &cfg, cmd };
	if (!code_init_and_upload(fd, &cfg, upload_callback, &arg)) {
		goto out;
	}

	printf("\n");

	char line[256];

	if (dump) {
		bool first = true;
		struct progress pg;

		while (offset < max) {
			unsigned chunk = length < cfg.chunklen ? length : cfg.chunklen;
			unsigned end = offset + chunk;

			if (!code_run(fd, &cfg)) {
				goto out;
			}

			for (; offset < end; offset += 16, first = false)
			{
				if (first) {
					printf("\rdump: patience...");
					fflush(stdout);
				}

				uint32_t val[4];

				do {
					// on the first iteration, the dump code reads the flash,
					// so we'll allow a generous timeout of 120 seconds.
					int pending = ser_select(fd, first ? 120000 : 100);
					if (pending <= 0) {
						if (!pending) {
							fprintf(stderr, "\nerror: timeout while reading line\n");
							goto out;
						}
					}

					if (!ser_read(fd, line, sizeof(line))) {
						goto out;
					} else if (!*line) {
						continue;
					}

					bool parseable = false;
					if (!code_parse_values(line, val, &parseable)) {
						if (!parseable) {
							if (strstr(line, "CRASH")) {
								fprintf(stderr, "\n%s\n", line);
								verbose = true;
								goto out;
							} else if (opt_verbosity) {
								printf("\n%s\n", line);
							}
							continue;
						} else {
							fprintf(stderr, "\nerror: invalid line '%s'\n", line);
							goto out;
						}
					}

					if (first) {
						progress_init(&pg, offset, length);
					}

					break;
				} while(true);

				unsigned i = 0;
				for (; i != 4; ++i) {
					val[i] = ntohl(val[i]);
				}

				if (fwrite(val, 16, 1, fp) != 1) {
					perror("\nerror: fwrite");
					goto out;
				}

				printf("\r%s: ", cmd);
				progress_add(&pg, 16);
				progress_print(&pg, stdout);
			}
		}

		printf("\n");
	} else if (!strcmp("exec", cmd)) {
		if (code_run(fd, &cfg)) {
			verbose = true;
		}
	}

	success = true;

out:
	fflush(fp);

	if (opt_codefile) {
		free(cfg.code);
	}

	if (verbose) {
		int pending = ser_select(fd, 100);
		if (pending > 0) {
			do {
				if (!ser_read(fd, line, sizeof(line))) {
					break;
				}
				fprintf(dump ? stderr : stdout, "%s\n", line);
			} while ((pending = ser_select(fd, 100)));
		}
		return dump ? false : true;
	}

	return success;
}

static bool resolve_offset_and_length(unsigned *off, unsigned *len, bool need_len)
{
	if (!opt_off) {
		fprintf(stderr, "error: no offset specified\n");
		return false;
	}

	if (!space) {
		if (need_len) {
			if (!opt_len) {
				fprintf(stderr, "error: no length specified\n");
				return false;
			} else if (!strtou(opt_off, off, false) || !strtou(opt_len, len, false)) {
				return false;
			}
		}

		return true;
	}

	struct bcm2_partition *part = NULL;

	if (!strtou(opt_off, off, true)) {
		part = bcm2_addrspace_find_partition(space, opt_off);
		if (!part) {
			fprintf(stderr, "error: partition '%s' not found in address space '%s'\n",
					opt_off, space->name);
			return false;
		}
		*off = part->offset;
	}

	if (opt_len || !part) {
		if (!opt_len) {
			if (need_len) {
				fprintf(stderr, "error: no length specified\n");
				return false;
			} else {
				return true;
			}
		} else if (!strtou(opt_len, len, false)) {
			return false;
		}
	} else {
		*len = part->size;
	}

	if (!need_len) {
		*len = 0;
	}

	uint32_t kseg1 = 0;

	if (!strcmp(space->name, "ram")) {
		kseg1 = *off & profile->kseg1mask;
		*off &= ~profile->kseg1mask;
	}

	uint32_t max = space->min - 1 + space->size;
	uint32_t end = *off - 1 + *len;
	if (*off < space->min || (space->size && (*off > max || end > max || *len > space->size))) {
		fprintf(stderr, "error: range 0x%08x-0x%08x is not valid for address space '%s'\n",
				*off, end, space->name);
		return false;
	} else if (profile->buflen && *len > profile->buflen) {
		fprintf(stderr, "error: %u exceeds maximum buffer length of %u\n", *len,
				profile->buflen);
		return false;
	}

	*off |= kseg1;

	return true;
}

static bool resolve_profile_and_space(int fd, const char *cmd)
{
	if (auto_detect_profile) {
		printf("%s: auto-detecting profile ... ", cmd);
		fflush(stdout);

		if (!detect_profile(fd)) {
			return false;
		}

		if (profile) {
			printf("%s (%s)\n", profile->name, opt_shell ?
					"shell" : "bootloader");
		} else {
			printf("failed; falling back to 'generic'\n");
			profile = bcm2_profile_find("generic");
		}
	}

	if (opt_space) {
		space = bcm2_profile_find_addrspace(profile, opt_space);
		if (!space) {
			fprintf(stderr, "error: address space '%s' is not defined by profile '%s'\n",
					opt_space, profile->name);
			return false;
		}
	} else {
		space = bcm2_profile_find_addrspace(profile, "ram");
	}

	return true;
}

static void usage_and_exit(int status)
{
	fprintf(status == 0 ? stdout : stderr,
			"Usage: bcm2dump [command] [options]...\n"
			"\n"
			"Commands:\n"
			"  dump            Dump ram/flash contents\n"
			"  write           Write to ram\n"
			"  exec            Write to ram and execute\n"
			"  imgscan         Scan for ProgramStore images\n"
			"\n"
			"Options:\n"
			"  -a <space>      Address space to use\n"
			"  -A              Append to output file\n"
			"  -C <binary>     Upload custom dump code file\n"
			"  -d <dev>        Serial tty to use\n"
			"  -f <filename>   Input/output file (depending on command)\n"
			"  -F              Force operation\n"
			"  -h              Show this screen\n"
			"  -L              List all available profiles; -P <profile> -L\n"
			"                  displays information about the selected profile\n"
			"  -n <bytes>      Number of bytes to dump; when using -o <part>\n"
			"                  this defaults to the partition size\n"
			"  -o <off|part>   Offset for dumping/writing. Either a number or\n"
			"                  a partition name.\n"
			"  -O <opt>=<val>  Override profile option\n"
			"  -P <profile>    Device profile to use\n"
			"  -S              Force slow ram dump mode\n"
			"  -K              Use CM shell dump mode\n"
			"  -v              Verbose operation\n"
			"  -vv             Very verbose operation\n"
			"  -vvv            For debugging\n"
			"\n"
			"Binary prefixes k/K (1024) and m/M (1024^2) are supported.\n"
			"\n"
			"bcm2dump " VERSION " Copyright (C) 2016 Joseph C. Lehner\n"
			"Licensed under the GNU GPLv3; source code is available at\n"
			"https://github.com/jclehner/bcm2utils\n"
			"\n");
	exit(status);
}

static bool is_valid_command(const char *cmd)
{
	const char *cmds[] = { "dump", "write", "exec", "imgscan" };
	unsigned i = 0;
	for (; i < ARRAY_SIZE(cmds); ++i) {
		if (!strcmp(cmds[i], cmd)) {
			return true;
		}
	}

	return false;
}

static int parse_options(int argc, char **argv)
{
	int c;
	opt_verbosity = 0;

	profile = bcm2_profile_find("generic");

	while ((c = getopt(argc, argv, "P:a:o:n:C:f:d:O:LhSAFKLv")) != -1) {
		switch (c) {
			case 'A':
				opt_append = true;
				break;
			case 'F':
				opt_force = true;
				break;
			case 'S':
				opt_slow = true;
				break;
			case 'K':
				opt_shell = true;
				auto_detect_profile = false;
				break;
			case 'h':
				usage_and_exit(0);
				break;
			case 'L':
			case 'O':
			case 'P':
			case 'v':
				if (!handle_common_opt(c, optarg, &opt_verbosity, &profile)) {
					return -1;
				}

				if (c == 'P' || c == 'O') {
					auto_detect_profile = false;
				}

				if (opt_verbosity >= 3) {
					ser_debug = true;
				}
				break;
			case 'a':
				opt_space = optarg;
				break;
			case 'o':
				opt_off = optarg;
				break;
			case 'n':
				opt_len = optarg;
				break;
			case 'C':
				opt_codefile = optarg;
				break;
			case 'f':
				opt_filename = optarg;
				break;
			case 'd':
				opt_ttydev = optarg;
				break;
			default:
				usage_and_exit(1);
				break;
		}
	}

	return optind;
}

static bool do_dump(int fd, uint32_t off, uint32_t len)
{
	if (!opt_filename) {
		fprintf(stderr, "error: no output file specified\n");
		return false;
	}

	FILE *fp = fopen(opt_filename, opt_force ? "w" : "a");
	if (!fp) {
		perror(opt_filename);
		return false;
	}

	bool ret = false;

	long fsize = ftell(fp);
	if (fsize < 0) {
		perror("error: ftell");
		goto out;
	} else if (fsize) {
		if (!opt_append && !opt_force) {
			fprintf(stderr, "error: file '%s' exists; use -F to overwrite, or -A to append\n", opt_filename);
			goto out;
		} else if (opt_append) {
			fsize = fsize > 1024 ? (fsize - 1024) : 0;
			if (fseek(fp, fsize, SEEK_SET) != 0) {
				perror("error: fseek");
				goto out;
			}

			off += fsize;
			len -= fsize;
		}
	}

	if (!opt_slow && !opt_shell) {
		ret = dump_write_exec(fd, "dump", off, len, fp);
	} else {
		if (space->mem) {
			printf("%s: %s 0x%08x-0x%08x\n", "dump", space->name, off, off - 1 + len);
			if (opt_slow) {
				ret = dump_slow(fd, off, len, fp);
			} else {
				ret = dump_shell(fd, off, len, fp);
			}
		} else {
			fprintf(stderr, "error: slow dump/shell dump is only available for ram\n");
		}
	}

out:
	fclose(fp);
	return ret;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		usage_and_exit(1);
	}

	const char *cmd;

	if (argv[1][0] != '-') {
		cmd = argv[1];
		argv += 1;
		argc -= 1;
	} else {
		cmd = NULL;
	}

	if (parse_options(argc, argv) == -1) {
		return 1;
	}

	if (!cmd || !is_valid_command(cmd)) {
		usage_and_exit(1);
	}

	if (!opt_ttydev) {
		fprintf(stderr, "error: no tty specified\n");
		return 1;
	}

	int fd = ser_open(opt_ttydev, profile->baudrate);
	if (fd < 0) {
		perror(opt_ttydev);
		return 1;
	}

	bool success = false;

	if (!resolve_profile_and_space(fd, cmd)) {
		goto out;
	}

	bool need_len = !strcmp(cmd, "dump");
	uint32_t off, len;
	if (!resolve_offset_and_length(&off, &len, need_len)) {
		goto out;
	}

	if (!strcmp(cmd, "dump")) {
		success = do_dump(fd, off, len);
	} else if (!strcmp(cmd, "write") || !strcmp(cmd, "exec")) {
		success = dump_write_exec(fd, cmd, off, len, NULL);
	} else if (!strcmp(cmd, "imgscan")) {
		fprintf(stderr, "error: command not implemented\n");
	} else {
		fprintf(stderr, "error: invalid command '%s'\n", cmd);
	}

out:
	close(fd);
	return success ? 0 : 1;
}

/**
 * bcm2-utils - bcm2cfg
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
 * along with nmrpflash.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <openssl/md5.h>
#include <openssl/aes.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <getopt.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "profile.h"
#include "nonvol.h"
#include "common.h"

// md5(16) + magic(74) + version(4) + size(2)
#define CFG_MIN_SIZE (16 + 74 + 4 + 2)

static struct bcm2_profile *profile = NULL;

static bool parse_hexstr(const char *hexstr, char *buf, size_t *len)
{
	size_t l = strlen(hexstr);
	if (!(l % 2) && (l <= *len * 2)) {
		size_t i = 0;
		for (; i < l && hexstr[i * 2]; ++i) {
			char ch[3];
			unsigned val;
			memcpy(ch, hexstr + (i * 2), 2);
			ch[2] = '\0';
			if (sscanf(ch, "%02x", &val) != 1) {
				fprintf(stderr, "error: bad hex char '%s'\n", ch);
				return false;
			}

			buf[i] = val & 0xff;
		}

		*len = i;
		return true;
	} else {
		fprintf(stderr, "error: bad hex string length %zu\n", l);
	}

	return false;
}

static void print_md5(FILE *fp, unsigned char *md5)
{
	unsigned i = 0;
	for (; i < 16; ++i) {
		fprintf(fp, "%02x", md5[i]);
	}
}

static bool calc_md5(unsigned char *buf, long size, unsigned char *md5)
{
	MD5_CTX c;
	MD5_Init(&c);
	MD5_Update(&c, buf, size);

	if (profile->cfg_md5key[0]) {
		char key[16];
		size_t len = sizeof(key);
		if (!parse_hexstr(profile->cfg_md5key, key, &len)) {
			fprintf(stderr, "error: failed to parse md5key of profile '%s'\n",
					profile->name);
			return false;
		}

		MD5_Update(&c, key, len);
	}

	MD5_Final(md5, &c);
	return true;
}

static int read_file(const char *filename, unsigned char **buf, size_t *len)
{
	*buf = NULL;

	FILE *fp = fopen(filename, "r");
	if (!fp) {
		perror(filename);
		return 1;
	}

	int err = 1;

	fseek(fp, 0, SEEK_END);
	*len = ftell(fp);
	if (*len < 0) {
		perror("ftell");
		goto out;
	}

	rewind(fp);

	*buf = malloc(*len + 16);
	if (!buf) {
		perror("malloc");
		goto out;
	}

	if (fread(*buf, *len, 1, fp) != 1) {
		perror("fread");
		goto out;
	}

	memset(*buf + *len - 16, 0, 16);

	err = 0;

out:
	if (err && *buf) {
		free(*buf);
	}

	fclose(fp);
	return err;
}

static bool xfwrite(const void *buf, size_t len, FILE *fp)
{
	if (fwrite(buf, len, 1, fp) != 1) {
		perror("fwrite");
		return false;
	}

	return true;
}

static int do_verify(unsigned char *buf, size_t len, int verbosity)
{
	unsigned char actual[16], expected[16];
	memcpy(actual, buf, 16);

	if (!calc_md5(buf + 16, len - 16, expected)) {
		return 1;
	}

	if (memcmp(actual, expected, 16)) {
		if (verbosity) {
			printf("bad checksum: ");
			print_md5(stdout, actual);
			printf(", expected ");
			print_md5(stdout, expected);
			printf("\n");
		}
		return 1;
	} else if (verbosity > 1) {
		printf("checksum ok : ");
		print_md5(stdout, actual);
		printf("\n");
	}

	return 0;
}

#define CRYPT_ENCRYPT 0
#define CRYPT_DECRYPT 1
#define CRYPT_PADBLK 2

static int do_crypt(unsigned char *buf, size_t len, const char *outfile, const char *keystr, const char *password, int flags)
{
	if (len < 16) {
		fprintf(stderr, "error: file too small\n");
		return 1;
	}

	unsigned char *obuf = NULL, *wbuf = NULL;
	int err = 1;

	if (password || keystr) {
		unsigned char key[32];

		if (password) {
			if (!profile->cfg_keyfun) {
				fprintf(stderr, "error: no key derivation function in profile '%s'\n",
						profile->name);
				return 1;
			}

			if (!profile->cfg_keyfun(password, key)) {
				fprintf(stderr, "error: key derivation function failed\n");
				return 1;
			}
		} else {
			size_t keylen = strlen(keystr);
			if (keylen != 64 || !parse_hexstr(keystr, (char*)key, &keylen)) {
				fprintf(stderr, "error: key must be a 64 character hex string\n");
				return 1;
			}
		}

		AES_KEY aes;

		if (flags & CRYPT_DECRYPT) {
			AES_set_decrypt_key(key, 256, &aes);
		} else {
			AES_set_encrypt_key(key, 256, &aes);
		}

		if (flags & CRYPT_PADBLK && flags & CRYPT_ENCRYPT) {
			len += 16;
		}

		obuf = malloc(len - 16);
		if (!obuf) {
			perror("malloc");
			return 1;
		}

		unsigned char *oblock = obuf;
		unsigned char *iblock = buf + 16;
		size_t remaining = len - 16;

		while (remaining >= 16) {
			if (flags & CRYPT_DECRYPT) {
				AES_decrypt(iblock, oblock, &aes);
			} else {
				AES_encrypt(iblock, oblock, &aes);
			}

			remaining -= 16;
			iblock += 16;
			oblock += 16;
		}

		if (remaining) {
			memcpy(oblock, iblock, remaining);
		}

		wbuf = obuf;
	} else {
		wbuf = buf + 16;
	}

	FILE *fp = fopen(outfile, "w");
	if (!fp) {
		perror(outfile);
		goto out;
	}

	unsigned char md5[16];
	if (!calc_md5(wbuf, len - 16, md5)) {
		goto out;
	}

	if (!xfwrite(md5, 16, fp) || !xfwrite(wbuf, len - 16, fp)) {
		goto out;
	}

	if (!password) {
		printf("new checksum: ");
		print_md5(stdout, md5);
		printf("\n");
	}

	err = 0;

out:
	free(obuf);
	fclose(fp);
	return err;
}

static int do_fix(unsigned char *buf, size_t len, const char *outfile)
{
	bool fixed = false;
	uint16_t *size = (uint16_t*)(buf + 16 + 74 + 4);
	if (ntohs(*size) != (len - 0x10)) {
		if (len > 0xffff) {
			fprintf(stderr, "error: input file exceeds maximum file size\n");
			return 1;
		}

		printf("updated size: %u -> %zu\n", ntohs(*size), len);
		*size = htons(len - 0x10);
		fixed = true;
	}

	int ret = 0;

	if (do_verify(buf, len, 0) != 0) {
		// this simply updates the md5 sum
		ret = do_crypt(buf, len, outfile, NULL, NULL, 0);
		fixed = true;
	}

	if (!ret && !fixed) {
		printf("nothing to fix\n");
	}

	return ret;
}

static char *magic_to_str(union bcm2_nv_group_magic *m)
{
	static char str[32];
	unsigned k = 0;

	str[0] = '\0';

	for (; k < 2; ++k) {
		unsigned i = 0;
		for (; i < 4; ++i) {
			char c = m->s[i];
			sprintf(str, k ? "%s%c" : "%s%02x", str, k ? (isprint(c) ? c : ' ') : c);
		}
		sprintf(str, "%s ", str);
	}

	return str;
}

static int do_list(unsigned char *buf, size_t len)
{
	if (len < CFG_MIN_SIZE) {
		fprintf(stderr, "error: file too short to be config file\n");
		return 1;
	}

	buf += 16;

	printf("  magic: %.74s\n", buf);
	buf += 74;

	printf("version: %d.%d\n", ntohs(*(uint16_t*)buf), ntohs(*(uint16_t*)(buf + 2)));
	buf += 4;

	uint16_t size = ntohs(*(uint16_t*)buf);
	buf += 2;

	printf("   size: %u b ", size);
	if (size != (len - 0x10)) {
		printf(" (does not match file size");
	}

	printf("\n");

	struct bcm2_nv_group *groups, *group;
	size_t remaining = 0;
	groups = group = bcm2_nv_parse_groups(buf, len - CFG_MIN_SIZE, &remaining);
	if (!groups) {
		return 1;
	}

	for (; group; group = group->next) {
		printf("  %5zx:  %s   %-40s (%d.%d) (%u bytes)", group->offset,
				magic_to_str(&group->magic), group->name, group->version[0],
				group->version[1], group->size);
		if (group->invalid) {
			printf(" (invalid)");
		}
		printf("\n");
	}

	if (remaining) {
		printf("  (failed to parse last %zu bytes)\n", remaining);
	}

	bcm2_nv_free_groups(groups);

	return 0;
}

static void usage(int exitstatus)
{
	fprintf(stderr,
			"Usage: bcm2cfg [options]\n"
			"\n"
			"Commands:\n"
			"  -V              Verify input file\n"
			"  -f              Fix checksum and file size\n"
			"  -d              Decrypt input file\n"
			"  -e              Encrypt input file\n"
			"  -l              List contents\n"
			"\n"
			"Options:\n"
			"  -h              Show help\n"
			"  -p <password>   Backup password\n"
			"  -k <key>        Backup key\n"
			"  -o <output>     Output file\n"
			"  -n              Ignore bad checksum\n"
			"  -L              List profiles\n"
			"  -P <profile>    Select device profile\n"
			"  -O <var>=<arg>  Override profile variable\n"
			"  -v              Verbose operation\n"
			"  -z              Pad before encrypting\n"
			"\n"
			"bcm2cfg " VERSION " Copyright (C) 2016 Joseph C. Lehner\n"
			"Licensed under the GNU GPLv3; source code is available at\n"
			"https://github.com/jclehner/bcm2utils\n"
			"\n");
	exit(exitstatus);
}


int main(int argc, char **argv)
{
	int verify = 0, fix = 0, decrypt = 0, encrypt = 0, list = 0;
	int noverify = 0, verbosity = 0, cryptflags = 0;
	const char *outfile = NULL;
	const char *infile = NULL;
	const char *password = NULL;
	const char *key = NULL;

	int c;
	opterr = 0;

	profile = bcm2_profile_find("generic");

	while ((c = getopt(argc, argv, "Vfdelzp:k:o:nvP:O:L")) != -1) {
		switch (c) {
			case 'V':
				verify = 1;
				break;
			case 'f':
				fix = 1;
				noverify = 1;
				break;
			case 'd':
				decrypt = 1;
				break;
			case 'e':
				encrypt = 1;
				break;
			case 'l':
				list = 1;
				break;
			case 'n':
				noverify = 1;
				break;
			case 'p':
				password = optarg;
				break;
			case 'k':
				key = optarg;
				break;
			case 'o':
				outfile = optarg;
				break;
			case 'L':
			case 'O':
			case 'P':
			case 'v':
				if (!handle_common_opt(c, optarg, &verbosity, &profile)) {
					return 1;
				}
				break;
			case 'z':
				cryptflags |= CRYPT_PADBLK;
				break;
			case 'h':
				usage(0);
				break;
			default:
				usage(1);
		}
	}

	if ((verify + fix + decrypt + encrypt + list) != 1) {
		usage(1);
	}

	if (optind == argc) {
		fprintf(stderr, "error: no input file specified\n");
		return 1;
	}

	infile = argv[optind];

	if (verify && noverify) {
		fprintf(stderr, "error: -n and -V are mutually exclusive\n");
		return 1;
	}

	if (password && key) {
		fprintf(stderr, "error: -p and -k are mutually exclusive\n");
		return 1;
	}

	if ((!password && !key) && (encrypt || decrypt)) {
		fprintf(stderr, "error: no password or key specified\n");
		return 1;
	}

	if (!outfile && (encrypt || decrypt)) {
		fprintf(stderr, "error: no output file specified\n");
		return 1;
	}

	if (!strcmp(profile->name, "generic")) {
		printf("warning: generic profile selected - ");
		if (verify) {
			printf("verification is likely to fail\n");
		} else {
			printf("disabling verification\n");
			noverify = 1;
		}
	}

	unsigned char *buf = NULL;
	size_t len = 0;

	if (read_file(infile, &buf, &len) != 0) {
		return 1;
	}

	int ret = 0;

	if (verify || !noverify) {
		 ret = do_verify(buf, len, verify ? 2 : 1);
	}

	if (!ret) {
		if (encrypt || decrypt) {
			cryptflags |= decrypt ? CRYPT_DECRYPT : CRYPT_ENCRYPT;
			ret = do_crypt(buf, len, outfile, key, password, cryptflags);
		} else if (list) {
			ret = do_list(buf, len);
		}
	}

	if (fix) {
		ret = do_fix(buf, len, outfile ? outfile : infile);
	}

	free(buf);
	return ret;
}


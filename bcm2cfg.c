/**
 * bcm2-utils - bcm2cfg
 * Copyright (C) 2016 Joseph Lehner <joseph.c.lehner@gmail.com>
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
#include <stdbool.h>
#include <getopt.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

// "TMM_TC7200\0\0\0\0\0\0"
uint8_t md5key[] = {
    0x54, 0x4d, 0x4d, 0x5f,
    0x54, 0x43, 0x37, 0x32,
    0x30, 0x30, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

static void gen_key(const char *password, unsigned char *key)
{
	unsigned i = 0;
	for (; i < 32; ++i) {
		key[i] = i & 0xff;
	}

	if (password && *password) {
		size_t len = strlen(password);
		if (len > 32) {
			len = 32;
		}
		memcpy(key, password, len);
	}
}

static void print_md5(FILE *fp, unsigned char *md5)
{
	unsigned i = 0;
	for (; i < 16; ++i) {
		fprintf(fp, "%02x", md5[i]);
	}
}

static void calc_md5(unsigned char *buf, long size, unsigned char *md5)
{
	MD5_CTX c;
	MD5_Init(&c);
	MD5_Update(&c, buf, size);
	MD5_Update(&c, md5key, sizeof(md5key));
	MD5_Final(md5, &c);
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

	*buf = malloc(*len);
	if (!buf) {
		perror("malloc");
		goto out;
	}

	if (fread(*buf, *len, 1, fp) != 1) {
		perror("fread");
		goto out;
	}

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
static int do_verify(unsigned char *buf, size_t len, bool verbose)
{
	unsigned char actual[16], expected[16];
	memcpy(actual, buf, 16);

	memset(buf, 0, 16);
	calc_md5(buf + 16, len - 16, expected);

	if (memcmp(actual, expected, 16)) {
		printf("bad checksum: ");
		print_md5(stdout, actual);
		printf(", expected ");
		print_md5(stdout, expected);
		printf("\n");
		return 1;
	} else if (verbose) {
		printf("checksum ok : ");
		print_md5(stdout, actual);
		printf("\n");
	}

	return 0;
}

static int do_crypt(unsigned char *buf, size_t len, const char *outfile, const char *password, bool decrypt)
{
	unsigned char block[16];
	int err = 1;

	FILE *fp = fopen(outfile, "w");
	if (!fp) {
		perror(outfile);
		return 1;
	}

	if (password) {
		unsigned char key[32];
		gen_key(password, key);

		AES_KEY aes;

		if (decrypt) {
			AES_set_decrypt_key(key, 256, &aes);
		} else {
			AES_set_encrypt_key(key, 256, &aes);
		}

		if (fseek(fp, 16, SEEK_SET) < 0) {
			perror("fseek");
			goto out;
		}

		unsigned char *p = buf;
		size_t remaining = len;

		while (remaining >= 16) {
			if (decrypt) {
				AES_decrypt(p, block, &aes);
			} else {
				AES_encrypt(p, block, &aes);
			}

			if (!xfwrite(block, 16, fp)) {
				goto out;
			}

			remaining -= 16;
			p += 16;
		}

		if (remaining) {
			if (!xfwrite(buf, remaining, fp)) {
				goto out;
			}
		}

		rewind(fp);
	}

	calc_md5(buf + 16, len - 16, block);

	if (!xfwrite(block, 16, fp)) {
		goto out;
	}

	if (!password) {
		printf("new checksum: ");
		print_md5(stdout, block);
		printf("\n");
	}

	err = 0;

out:
	fclose(fp);
	return err;
}

static int do_fixmd5(unsigned char *buf, size_t len, const char *outfile)
{
	return do_crypt(buf, len, outfile, NULL, false);
}

static void usage()
{
	fprintf(stderr,
			"usage: bcm2cfg [options]\n"
			"\n"
			"operations:\n"
			"  -v            verify input file\n"
			"  -f            fix checksum\n"
			"  -d            decrypt input file\n"
			"  -e            encrypt input file\n"
			"\n"
			"options:\n"
			"  -l            dump backup file\n"
			"  -p <password> backup password\n"
			"  -o <output>   output file\n"
			"  -n            ignore bad checksum\n"
#if 0
			"  -P <profile>  device profile\n"
			"\n"
			"profiles:\n"
#endif
			"\n");
	exit(1);
}


int main(int argc, char **argv)
{
	int verify = 0, fixmd5 = 0, decrypt = 0, encrypt = 0, list = 0;
	int noverify = 0;
	const char *outfile = NULL;
	const char *infile = NULL;
	const char *password = NULL;
	const char *profile = NULL;

	int c;
	opterr = 0;

	while ((c = getopt(argc, argv, "vfdelp:o:n")) != -1) {
		switch (c) {
			case 'v':
				verify = 1;
				break;
			case 'f':
				fixmd5 = 1;
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
			case 'o':
				outfile = optarg;
				break;
			default:
				usage();
		}
	}

	if ((verify + fixmd5 + decrypt + encrypt + list) != 1) {
		usage();
		return 1;
	}

	if (optind == argc) {
		fprintf(stderr, "error: no input file specified\n");
		return 1;
	}

	infile = argv[optind];

	if (verify && noverify) {
		fprintf(stderr, "error: do not use -n with -v\n");
		return 1;
	}

	if (!password && (encrypt || decrypt)) {
		fprintf(stderr, "error: no password specified\n");
		return 1;
	}

	if (!outfile && (encrypt || decrypt)) {
		fprintf(stderr, "error: no output file specified\n");
		return 1;
	}

	unsigned char *buf = NULL;
	size_t len = 0;

	if (read_file(infile, &buf, &len) != 0) {
		return 1;
	}

	int ret = 0;

	if (verify || !noverify) {
		 ret = do_verify(buf, len, !noverify);
	}

	if (!ret) {
		if (encrypt || decrypt) {
			ret = do_crypt(buf, len, outfile, password, decrypt);
		} else if (fixmd5) {
			ret = do_fixmd5(buf, len, outfile ? outfile : infile);
		} else if (list) {
			fprintf(stderr, "error: not implemented\n");
			ret = 1;
		}
	}

	free(buf);
	return ret;
}


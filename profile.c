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
 * along with nmrpflash.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <string.h>
#include <stdio.h>
#include "profile.h"
#include "common.h"

static bool keyfun_tc7200(const char *password, unsigned char *key)
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

	return true;
}

static bool keyfun_dumb(const char *password, unsigned char *key)
{
	memset(key, 0, 32);

	if (password) {
		size_t len = strlen(password);
		if (len > 32) {
			len = 32;
		}
		memcpy(key, password, len);
	}

	return true;
}

struct bcm2_profile bcm2_profiles[] = {
	{
		.name = "generic",
		.pretty = "Generic Profile",
		.baudrate = 115200,
		.spaces = {
			{
				.name = "ram",
				.ram = true
			},
		}
	},
	{
		.name = "twg850",
		.pretty = "Thomson TWG-850",
		.cfg_keyfun = &keyfun_dumb,
	},
	{
		.name = "twg870",
		.pretty = "Thomson TWG-870",
		.cfg_keyfun = &keyfun_dumb,
	},
	{
		.name = "tc7200",
		.pretty = "Technicolor TC-7200/TC-7200.U",
		.baudrate = 115200,
		.pssig = 0xa825,
		.loadaddr = 0x84010000,
		.buffer = 0x85f00000,
		.kseg1mask = 0x20000000,
		.printf = 0x83f8b0c0,
		.scanf = 0x83f8ba94,
		.cfg_md5key = "544d4d5f544337323030000000000000",
		.cfg_keyfun = &keyfun_tc7200,
		.cfg_defkeys = {
			"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
			"0000000000000000000000000000000000000000000000000000000000000000"
		},
		.magic = { 0x83f8e618, "2.4.0alpha18p1" },
		.spaces = {
			{
				.name = "ram",
				.min = 0x80000000,
				.size = 128 * 1024 * 1024,
				.parts = {
					{ "bootloader", 0x83f80000, 0x020000 },
					{ "image1/2",   0x85f00000, 0x6c0000 },
					{ "linux",      0x87000000, 0x480000 }
				}
			},
			{
				.name = "spi",
				.size = 0x100000,
				.parts = {
					{ "bootloader", 0x00000, 0x10000 },
					{ "permnv",     0x10000, 0x10000 },
					{ "dynnv",      0x20000, 0xe0000 }
				},
				.read = { 
					.addr = 0x83f81298, 
					.mode = BCM2_READ_FUNC_OBL,
				},
			},
			{
				.name = "nand",
				.size = 64 * 1024 * 1024,
				.parts = {
					{ "linuxapps", 0x0000000, 0x19c0000 },
					{ "image1",    0x19c0000, 0x06c0000 },
					{ "image2",    0x2080000, 0x06c0000 },
					{ "linux",     0x2740000, 0x0480000 },
					{ "linuxkfs",  0x2bc0000, 0x1200000 },
					{ "dhtml",     0x3dc0000, 0x0240000 },
				},
				.read = { 
					.addr = 0x83f831b4, 
					.mode = BCM2_READ_FUNC_BOL,
					.patch = {
						{ 0x83f83380, 0x11000017 },
					}
				},
			}
		}
	},
	// end marker
	{ .name = "" },
};

void *find_by_name(const char *name, void *list, size_t elemsize)
{
	char *p = list;

	for (; *p; p += elemsize) {
		if (!strcmp(p, name)) {
			return p;
		}
	}

	return NULL;
}

struct bcm2_profile *bcm2_profile_find(const char *name)
{
	return find_by_name(name, bcm2_profiles, sizeof(*bcm2_profiles));
}

struct bcm2_addrspace *bcm2_profile_find_addrspace(struct bcm2_profile *profile,
		const char *name)
{
	struct bcm2_addrspace *s = find_by_name(name, profile->spaces, sizeof(*profile->spaces));
	if (s && !strcmp(s->name, "ram")) {
		s->ram = true;
	}

	return s;
}

struct bcm2_partition *bcm2_addrspace_find_partition(struct bcm2_addrspace *addrspace,
		const char *name)
{
	return find_by_name(name, addrspace->parts, sizeof(*addrspace->parts));
}

/**
 * bcm2-utils
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
 * along with bcm2-utils.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "profile.h"

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

struct bcm2_profile bcm2_profiles[] = {
	{
		.name = "cg3000",
		.pretty = "Netgear CG3000",
		.pssig = 0xa0f7,
		.cfg_md5key = "3250736c633b752865676d64302d2778",
		.spaces = {
				{ .name = "ram" },
		},
	},
	{
		.name = "twg850",
		.pretty = "Thomson TWG850-4",
		.baudrate = 115200,
		.pssig = 0xa815,
		.blsig = 0x3345,
		.kseg1mask = 0x20000000,
		.cfg_md5key = "544d4d5f5457473835302d3400000000",
		.magic = {
			{ 0x80f89da0, "Oct 16 2007" }
		},
		.spaces = {
			{
				.name = "ram",
				.min = 0x80000000,
				.size = 32 * 1024 * 1024,
				.parts = {
					{ "bootloader", 0x80f80000, 0x010000 }
				}
			},
#if 0
			{
				.name = "flash",
				.min = 0xbf000000,
				.mem = BCM2_MEM_RW,
				.size = 8 * 1024 * 1024,
				.parts = {
					{ "image2",     0xbf000000, 0x3e0000 },
					{ "dynnv",      0xbf3e0000, 0x020000, "dyn" },
					{ "bootloader", 0xbf400000, 0x010000 },
					{ "image1",     0xbf410000, 0x3e0000 },
					{ "permnv",     0xbf7f0000, 0x010000, "perm" }
				}
			},
#else
			{
				.name = "flash",
				.size = 8 * 1024 * 1024,
				.parts = {
					{ "image2",     0x000000, 0x3e0000 },
					{ "dynnv",      0x3e0000, 0x020000, "dyn" },
					{ "bootloader", 0x400000, 0x010000 },
					{ "image1",     0x410000, 0x3e0000 },
					{ "permnv",     0x7f0000, 0x010000, "perm" }
				}
			},
#endif
		}
	},
	{
		.name = "tcw770",
		.pretty = "Thomson TCW770",
		.cfg_md5key = "544d4d5f544357373730000000000000",
		.spaces = {
				{ .name = "ram" },
		},
	},
	{
		.name = "twg870",
		.pretty = "Thomson TWG870",
		.baudrate = 115200,
		.pssig = 0xa81b,
		.blsig = 0x3380,
		.cfg_md5key = "544d4d5f545747383730000000000000",
		.cfg_defkeys = {
			"0001020304050607080910111213141516171819202122232425262728293031",
		},
		.magic = {
			{ 0x82f00014, "TWG870" }
		},
		.spaces = {
			{
				.name = "ram",
				.min = 0x80000000,
				.size = 64 * 1024 * 1024,
				.parts = {
					{ "image",   0x82f00000, 0x3e0000 },
					{ "bootloader", 0x83f80000, 0x010000 },
				}
			},
			{
				.name = "flash",
				.size = 8 * 1024 * 1024,
				.parts = {
					{ "bootloader", 0x000000, 0x008000 },
					{ "unknown",    0x008000, 0x008000 },
					{ "permnv",     0x010000, 0x010000, "perm" },
					{ "image1",     0x020000, 0x3e0000 },
					{ "image2",     0x400000, 0x3e0000 },
					{ "dynnv",      0x7e0000, 0x010000, "dyn" }
				}
			},
		}
	},
	{
		.name = "tc7200",
		.pretty = "Technicolor TC7200",
		.baudrate = 115200,
		.pssig = 0xa825,
		.blsig = 0x3386,
		.loadaddr = 0x84010000,
		.buffer = 0x85f00000,
		.kseg1mask = 0x20000000,
		.printf = 0x83f8b0c0,
		.sscanf = 0x83f8ba94,
		.getline = 0x83f8ad10,
		.cfg_md5key = "544d4d5f544337323030000000000000",
		.cfg_keyfun = &keyfun_tc7200,
		.cfg_defkeys = {
			"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
		},
		.magic = {
			{ 0x83f8e618, "2.4.0alpha18p1" },
			{ 0x85f00014, "TC7200" }
		},
		.spaces = {
			{
				.name = "ram",
				.min = 0x80000000,
				.size = 128 * 1024 * 1024,
				.parts = {
					{ "bootloader", 0x83f80000, 0x020000 },
					{ "image",   0x85f00000, 0x6c0000 },
					{ "linux",      0x87000000, 0x480000 }
				}
			},
			{
				.name = "nvram",
				.size = 0x100000,
				.parts = {
					{ "bootloader", 0x00000, 0x10000 },
					{ "permnv",     0x10000, 0x10000, "perm" },
					{ "dynnv",      0xe0000, 0x20000, "dyn" }
				},
#if 0
				.read = {
					{
						.addr = 0x83f81298,
						.intf = BCM2_INTF_BLDR,
						.mode = BCM2_READ_FUNC_OBL,
					}
				},
				.write = {
					{
						.addr = 0x83f810bc,
						.intf = BCM2_INTF_BLDR,
						.mode = BCM2_READ_FUNC_OBL,
					}
				},
				.erase = {
					{
						.addr = 0x83f814e0,
						.intf = BCM2_INTF_BLDR,
						.mode = BCM2_ERASE_FUNC_OL,
					}
				},
#endif
			},
			{
				.name = "flash",
				.size = 64 * 1024 * 1024,
				.parts = {
					{ "linuxapps", 0x0000000, 0x19c0000, "image3e" },
					{ "image1",    0x19c0000, 0x06c0000 },
					{ "image2",    0x2080000, 0x06c0000 },
					{ "linux",     0x2740000, 0x0480000, "image3" },
					{ "linuxkfs",  0x2bc0000, 0x1200000, "" },
					{ "dhtml",     0x3dc0000, 0x0240000 },
				},
			},
		},
		.versions = {
			{
				.intf = BCM2_INTF_BLDR,
				.loadaddr = 0x84010000,
				.buffer = 0x85f00000
			},
			{
				.version = "2.4.0alpha18p1",
				.intf = BCM2_INTF_BLDR,
				.magic = { 0x83f8e618, "2.4.0alpha18p1" },
				.printf = 0x83f8b0c0,
				.sscanf = 0x83f8ba94,
				.getline = 0x83f8ad10,
				.spaces = {
					{
						.name = "flash",
						.read = {
								.addr = 0x83f831b4,
								.mode = BCM2_READ_FUNC_BOL,
								.patch = {{ 0x83f83380, 0x10000017 }}
						},
						.write = { 0x83f82e98, BCM2_READ_FUNC_OBL },
						.erase = { 0x83f82c08, BCM2_ERASE_FUNC_OS },
					},
					{
						.name = "nvram",
						.read = { 0x83f81298, BCM2_READ_FUNC_OBL },
						.write = { 0x83f810bc, BCM2_READ_FUNC_OBL },
						.erase = { 0x83f814e0, BCM2_ERASE_FUNC_OL },
					}
				}
			},
			{
				.intf = BCM2_INTF_BFC,
				.loadaddr = 0x80004000,
				.buffer = 0x85f00000,
				.buflen = 0x19c0000
			},
			{
				.version = "STD6.02.41",
				.intf = BCM2_INTF_BFC,
				.magic = { 0x814e8eac, "STD6.02.41" },
				.spaces = {
						{
							.name = "flash",
							.open = { 0x803f704c, BCM2_ARGS_OE },
							.read = { 0x803f6af8, BCM2_READ_FUNC_BOL,
									.patch = {{ 0x803f6ca4, 0x10000018 }},
							}
						},
						{
							"nvram",
						}
				}
			},
			{
				.version = "STD6.01.27",
				.intf = BCM2_INTF_BFC,
				.magic = { 0x85f00014, "TC7200U-D6.01.27" },
				.spaces = {
						{
							"flash",
							.open = { 0x8039eabc, BCM2_ARGS_OE },
							.read = { 0x8039e868, BCM2_READ_FUNC_BOL,
									.patch = {{ 0x8039e9bc, 0x10000018 }},
							}
						},
						{
							"nvram",
						}
				}
			},
		},
	},
	{
		.name = "generic",
		.pretty = "Generic Profile",
		.baudrate = 115200,
		.cfg_defkeys = {
			"0000000000000000000000000000000000000000000000000000000000000000",
			"0001020304050607080910111213141516171819202122232425262728293031",
			"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
		},
		.spaces = {
			{
				.name = "ram",
			},
			// this hack enables us to use the bfc_flash dumper on
			// any device (provided you specify a dump size).
			{
				.name = "flash",
				.parts = {
						{ "bootloader" },
						{ "dynnv", 0, 0, "dyn" },
						{ "permnv", 0, 0, "perm" },
						{ "image1" },
						{ "image2" },
						{ "image3" },
						{ "image3e" },
						{ "dhtml" }
				},
			}
		},
	},
	// end marker
	{ .name = "" },
};

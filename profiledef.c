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

static bool keyfun_tc7200(const char *password, unsigned char *key, size_t size)
{
	unsigned i = 0;
	for (; i < size; ++i) {
		key[i] = i & 0xff;
	}

	if (password && *password) {
		size_t len = strlen(password);
		if (len > size) {
			len = size;
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
		.cfg_md5key = "3250736c633b752865676d64302d2778",
		.cfg_defkeys = {
			"0000000000000000000000000000000000000000000000000000000000000000",
			"0001020304050607080910111213141516171819202122232425262728293031",
			"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
		},
		.spaces = {
			{
				.name = "ram",
				.parts = {
						{ "bootloader", 0xbfc00000, 0 }
				},
			},
			// this hack enables us to use the bfc_flash dumper on
			// any device (provided you specify a dump size).
			{
				.name = "flash",
				.parts = {
						{ "bootloader" },
						{ "dynnv", 0, 0, "dyn" },
						{ "vennv", 0, 0, "ven" },
						{ "permnv", 0, 0, "perm" },
						{ "image1" },
						{ "image2" },
						{ "image3" },
						{ "image3e" },
						{ "linux" },
						{ "linuxapps" },
						{ "linuxkfs" },
						{ "dhtml" }
				},
			}
		},
	},
#if 1
	{
		.name = "debug",
		.pretty = "Debug",
		.magic = {{ 0x80010000, "DBUG" }},
		.spaces = {
			{
				.name = "ram",
			},
			{
				.name = "flash",
				.size = 0x100000,
				.parts = {
					{ "bootloader", 0x00000, 0x10000 },
					{ "permnv",     0x10000, 0x10000, "perm" },
					{ "dynnv",      0xe0000, 0x20000, "dyn" }
				},
			},
		},
		.versions = {
			{
				.intf = BCM2_INTF_BLDR,
				.rwcode = 0x85eff000,
				.buffer = 0x85f00000,
				.printf = 0x83f8b0c0,
				.sscanf = 0x83f8ba94,
				.getline = 0x83f8ad10,
				.spaces = {
					{
						.name = "flash",
						.read = { 0x80010068, BCM2_READ_FUNC_BOL },
						.write = { 0x80010034, 0 },
						.erase = { 0x80010004, BCM2_ERASE_FUNC_OL },
					}
				}
			},
		}
	},
#endif
#if 0
	// this is a 'gwsdyn' format!
	{
		.name = "vmdg280",
		.pretty = "Netgear VMDG280",
		.cfg_flags = BCM2_CFG_FMT_GWS_FULL_ENC | BCM2_CFG_ENC_SUB_16x16 | BCM2_CFG_FMT_GWS_CLEN_PREFIX,
		.spaces = {
				{ .name = "ram" },
		},
	},
#endif
	{
		.name = "cg3000",
		.pretty = "Netgear CG3000",
		.pssig = 0xa0f7,
		.spaces = {
				{ .name = "ram" },
		},
	},
	{
		.name = "cg3101",
		.pretty = "Netgear CG3101",
		.pssig = 0xa0e7,
		.cfg_flags = BCM2_CFG_ENC_3DES_ECB | BCM2_CFG_FMT_GWS_FULL_ENC |
			BCM2_CFG_PAD_ANSI_ISH,
		.cfg_md5key = "3250736c633b752865676d64302d2778",
		.cfg_defkeys = {
			// 3 keys for 3DES
			"8890697cec4823e2" "ea3ad4c87f13978e" "46ac783a2d843e11"
		},
		.spaces = {
				{ .name = "ram" },
		},
	},
	{
		.name = "cbw383zn",
		.pretty = "NetMASTER CBW-383ZN",
		.arch = BCM2_3383,
		.pssig = 0x8364,
		.blsig = 0x3383,
		.cfg_flags = BCM2_CFG_ENC_DES_ECB | BCM2_CFG_FMT_GWS_FULL_ENC,
		.cfg_md5key = "3250736c633b752865676d64302d2778",
		.cfg_defkeys = { "1122334455667788" },
		.magic = {
			{ 0x83f8a9ac, "2.4.0" },
		},
		.spaces = {
			{
				.name = "ram",
				.min = 0x80000000,
				.size = 128 * 1024 * 1024,
				.parts = {
					{ "image",      0x85f00000, 0xff0000 },
					{ "bootloader", 0x83f80000, 0x020000 },
				}
			},
			{
				.name = "flash",
				.size = 32 * 1024 * 1024,
				.parts = {
					{ "bootloader", 0x0000000, 0x010000 },
					{ "permnv",     0x0010000, 0x010000, "perm" },
					{ "image1",     0x0030000, 0xfe0000 },
					{ "image2",     0x1000000, 0xff0000 },
					{ "dynnv",      0x1ff0000, 0x010000, "dyn" }
				}
			},
		},
		.versions = {
			{
				.intf = BCM2_INTF_BFC,
				.rwcode = 0x80002000,
				.options = {
					{ "bfc:flash_read_direct", { true }},
				}
			},
			{
				.version = "0081.799.009",
				.intf = BCM2_INTF_BFC,
				.magic = { 0x80dc48d0, "009" },
				.options = {
					{ "bfc:conthread_instance", { 0x81204074 }},
					{ "bfc:conthread_priv_off", { 0x74 }},
				}
			},
			{
				.intf = BCM2_INTF_BLDR,
				.rwcode = 0x84010000,
				.buffer = 0x85f00000
			},
			{
				.version = "2.4.0",
				.intf = BCM2_INTF_BLDR,
				.magic = { 0x83f8a9ac, "2.4.0" },
			},
		},
	},
	{
		.name = "ch7485e",
		.pretty = "Compal CH7485E",
		.baudrate = 115200,
		.pssig = 0xa923,
		.kseg1mask = 0x20000000,
		.magic = {
			{ 0x83f8ea40, "2.5.0beta1" },
		},
		.spaces = {
			{
				.name = "ram",
				.min = 0x80000000,
				.size = 256 * 1024 * 1024,
				.parts = {
					{ "bootloader", 0x83f80000, 0x020000 },
				}
			},
			{
				.name = "nvram",
				.size = 512 * 1024,
				.parts = {
					{ "bootloader", 0x00000, 0x10000 },
					{ "dynnv",      0x10000, 0x20000, "dyn" },
					{ "permnv",     0x40000, 0x40000, "perm" },
				},
			},
			{
				.name = "flash",
				.size = 128 * 1024 * 1024,
				.parts = {
					{ "linuxapps", 0x0000000, 0x4c40000, "image3e" },
					{ "image1",    0x4c40000, 0x0d80000 },
					{ "image2",    0x59c0000, 0x0d80000 },
					{ "linux",     0x6740000, 0x0480000, "image3" },
					{ "linuxkfs",  0x6bc0000, 0x1200000, "" },
					{ "dhtml",     0x7dc0000, 0x0240000 },
				}
			},
		},
		.versions = {
			{
				.intf = BCM2_INTF_BLDR,
				.rwcode = 0x84010000,
				.buffer = 0x85f00000
			},
			{
				.version = "2.5.0beta1",
				.intf = BCM2_INTF_BLDR,
				.magic = { 0x83f8ea40, "2.5.0beta1" },
				.printf = 0x83f8b3ec,
				.spaces = {
					{
						.name = "flash",
						.read = {
							.addr = 0x83f83b38,
							.mode = BCM2_READ_FUNC_BOL,
							.patch = {{ 0x83f83cdc, 0x10000018 }}
						},
					},
					{
						.name = "nvram",
						.read = { 0x83f811bc, BCM2_READ_FUNC_OBL },
					}
				}
			},
		},
	},
	{
		.name = "c6300bd",
		.pretty = "Netgear C6300BD",
		.arch = BCM2_3384,
		.baudrate = 115200,
		.pssig = 0xa0eb,
		.kseg1mask = 0x20000000,
		.magic = {
			{ 0x83f8ecc8, "2.5.0alpha8R2" },
		},
		.cfg_flags = BCM2_CFG_ENC_DES_ECB | BCM2_CFG_FMT_GWS_FULL_ENC,
		.cfg_md5key = "3250736c633b752865676d64302d2778",
		.cfg_defkeys = { "aabbccddeeffaabb" },
		.spaces = {
			{
				.name = "ram",
				.min = 0x80000000,
				.size = 256 * 1024 * 1024,
				.parts = {
					{ "bootloader", 0x83f80000, 0x020000 },
				}
			},
			{
				.name = "nvram",
				.size = 512 * 1024,
				.parts = {
					{ "bootloader", 0x00000, 0x10000 },
					{ "permnv",     0x10000, 0x20000, "perm" },
					{ "vennv",      0x30000, 0x10000, "ven" },
					{ "dynnv",      0x40000, 0x40000, "dyn" },
				},
			},
			{
				.name = "flash",
				.size = 128 * 1024 * 1024,
				.parts = {
					{ "linuxapps", 0x0000000, 0x23c0000, "image3e" },
					{ "image1",    0x23c0000, 0x0900000 },
					{ "image2",    0x2cc0000, 0x0900000 },
					{ "linux",     0x35c0000, 0x2400000, "image3" },
					{ "linuxkfs",  0x59c0000, 0x2400000, "" },
					{ "dhtml",     0x7dc0000, 0x0240000 },
				}
			},
		},
		.versions = {
			{
				.intf = BCM2_INTF_BLDR,
				.rwcode = 0x84010000,
				.buffer = 0x85f00000
			},
			{
				.version = "2.5.0alpha8R2",
				.intf = BCM2_INTF_BLDR,
				.magic = { 0x83f8ecc8, "2.5.0alpha8R2" },
				.printf = 0x83f8b670,
				.spaces = {
					{
						.name = "flash",
						.read = {
							.addr = 0x83f83740,
							.mode = BCM2_READ_FUNC_BOL,
							.patch = {{ 0x83f838e4, 0x10000018 }}
						},
					},
					{
						.name = "nvram",
						.read = { 0x83f81328, BCM2_READ_FUNC_OBL },
					}
				}
			},
		},
	},
	{
		.name = "sbg6580",
		.pretty = "Motorola Surfboard SBG6580",
		.pssig = 0xc055,
		.cfg_flags = BCM2_CFG_ENC_MOTOROLA | BCM2_CFG_FMT_GWS_FULL_ENC,
		.cfg_md5key = "3250736c633b752865676d64302d2778",
		.spaces = {
			{ .name = "ram" },
		},
	},
	{
		.name = "fast3686",
		.pretty = "Sagemcom F@ST 3686",
		.arch = BCM2_3384,
		.cfg_flags = BCM2_CFG_ENC_XOR | BCM2_CFG_FMT_GWS_FULL_ENC | BCM2_CFG_DATA_USERIF_ALT,
		.cfg_md5key = "3250736c633b752865676d64302d2778",
		.cfg_defkeys = { "80" },
		.pssig = 0xd06e,
		.blsig = 0x3384,
		.kseg1mask = 0x20000000,
		.magic = {
			{ 0x83f8f188, "2.5.0beta8" },
		},
		.spaces = {
			{
				.name = "ram",
				.min = 0x80000000
			},
			{
				.name = "nvram",
				.size = 4 * 1024 * 1024,
				.parts = {
					{ "bootloader", 0x000000, 0x10000 },
					{ "permnv",     0x010000, 0x20000, "perm" },
					{ "dynnv",      0x3c0000, 0x40000, "dyn" },
				}
			},
			{
				.name = "flash",
				.size = 128 * 1024 * 1024,
				.parts = {
					{ "linuxapps", 0x0000000, 0x4c40000, "image3e" },
					{ "image1",    0x4c40000, 0x0d80000 },
					{ "image2",    0x59c0000, 0x0d80000 },
					{ "linux",     0x6740000, 0x0480000, "image3" },
					{ "linuxkfs",  0x6bc0000, 0x1200000, "" },
					{ "dhtml",     0x7dc0000, 0x0240000 },
				}
			},
		},
		.versions = {
			{
				.intf = BCM2_INTF_BFC,
				.rwcode = 0x80002000,
				.buffer = 0x88000000,
				.options = {
					BCM2_VAL_STR("bfc:su_password", "$agem001"),
					BCM2_VAL_U32("bfc:conthread_priv_off", 0x74),
				},
			},
			{
				.version = "CVA-SIP_3.601.0",
				.intf = BCM2_INTF_BFC,
				.magic = { 0x812df0e5, "CVA-SIP_3.601.0-20190521" },
				.options = {
					{ "bfc:conthread_instance", { 0x818dd3c0 }},
				},
				.spaces = {
						{
							.name = "flash",
							.open = { 0x80438e6c, BCM2_ARGS_OE },
							.read = { 0x80438934, BCM2_READ_FUNC_BOL,
									.patch = {{ 0x80438ac4, 0x10000018 }},
							}
						},
				}
			},
			{
				.version = "DNA_3.490.0",
				.intf = BCM2_INTF_BFC,
				.magic = { 0x81082fa8, "FAST3686_DNA_3.490.0-T3-20200429" },
				.spaces = {
						{
							.name = "flash",
							.open = { 0x803e1940, BCM2_ARGS_OE },
							.read = { 0x803e1408, BCM2_READ_FUNC_BOL,
									.patch = {{ 0x803e1598, 0x10000018 }},
							}
						},
				}
			}
		},
	},
	{
		.name = "fast3890",
		.pretty = "Sagemcom F@ST 3890",
		.arch = BCM2_3390,
		.pssig = 0x3390,
		.kseg1mask = 0x20000000,
		.magic = {
			{ 0x83e05bb8, "2.7.0alpha4" }
		},
		.spaces = {
			{
				.name = "ram",
				.min = 0x80000000
			},
		},
		.versions = {
			{
				.intf = BCM2_INTF_BFC,
				.rwcode = 0x80002000,
				.options = {
					BCM2_VAL_STR("bfc:su_password", "sagem"),
					BCM2_VAL_U32("bfc:conthread_priv_off", 0x74),
				},
			},
			{
				.version = "50.10.11.T1",
				.intf = BCM2_INTF_BFC,
				.magic = { 0x809864d9, "50.10.11.T1" },
				.options = {
					{ "bfc:conthread_instance", { 0x80c2a68c }},
				}
			}
		},
	},
	{
		.name = "fast3286",
		.pretty = "Sagemcom F@ST 3286",
		.cfg_flags = BCM2_CFG_ENC_XOR | BCM2_CFG_FMT_GWS_FULL_ENC | BCM2_CFG_DATA_USERIF_ALT_SHORT,
		.cfg_md5key = "3250736c633b752865676d64302d2778",
		.cfg_defkeys = { "80" },
		.spaces = {
			{
				.name = "ram",
			},
			{
				// FIXME
				.name = "flash",
				.parts = {
						{ "bootloader" },
						{ "dynnv", 0, 0, "dyn" },
						{ "permnv", 0, 0, "perm" },
						{ "image1" },
						{ "image2" },
						{ "image3" },
						{ "image3e" },
						{ "linux" },
						{ "linuxapps" },
						{ "linuxkfs" },
						{ "dhtml" }
				},
			}
		},
		.versions = {
			{
				.intf = BCM2_INTF_BFC,
				.options = {
					BCM2_VAL_STR("bfc:su_password", "$agem001"),
				},
			},
		},
	},
	{
		.name = "mg7550",
		.pretty = "Motorola MG7550",
		.pssig = 0x7550,
		.kseg1mask = 0x20000000,
		.cfg_md5key = "3250736c633b752865676d64302d2778",
		.spaces = {
			{
				.name = "ram",
				.min = 0x80000000,
				.size = 256 * 1024 * 1024,
			},
		},
		.versions = {
			{
				.intf = BCM2_INTF_BFC,
				.rwcode = 0x80002000,
			},
			{
				.version = "5.7.1.27",
				.intf = BCM2_INTF_BFC,
				.magic = { 0x80eb8a91, "5.7.1.27" },
				.options = {
					{ "bfc:conthread_instance", { 0x812efff4 }},
					{ "bfc:conthread_priv_off", { 0x74 }},
				}
			}
		},
	},
	{
		.name = "mb7420",
		.pretty = "Motorola MB7420",
		.pssig = 0x3843,
		.kseg1mask = 0x20000000,
		.spaces = {
			{
				.name = "ram",
				.min = 0x80000000,
			},
		},
		.versions = {
			{
				.intf = BCM2_INTF_BFC,
				.rwcode = 0x80002000,
			},
			{
				.version = "5.7.1.19 MAC14",
				.intf = BCM2_INTF_BFC,
				.magic = { 0x80624d91, "5.7.1.19 MAC14" },
				.options = {
					{ "bfc:conthread_instance", { 0x8071e198 }},
					{ "bfc:conthread_priv_off", { 0x74 }},
				}
			},
			{
				.version = "5.7.1.19",
				.intf = BCM2_INTF_BFC,
				.magic = { 0x80624d91, "5.7.1.19" },
				.options = {
					{ "bfc:conthread_instance", { 0x8071e170 }},
					{ "bfc:conthread_priv_off", { 0x74 }},
				}
			},
		},
	},
	{
		.name = "twg850",
		.pretty = "Thomson TWG850-4",
		.arch = BCM2_3368,
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
		.arch = BCM2_3380,
		.baudrate = 115200,
		.pssig = 0xa81b,
		.blsig = 0x3380,
		.cfg_flags = BCM2_CFG_ENC_AES256_ECB | BCM2_CFG_PAD_ZEROBLK |
			BCM2_CFG_FMT_GWS_PAD_OPTIONAL,
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
		.name = "evm3236",
		.pretty = "Ubee EVM3236",
		.baudrate = 115200,
		.kseg1mask = 0x20000000,
		.magic = {
			{ 0x807023d4, "EVM3236" },
		},
		.spaces = {
			{
				.name = "ram",
				.min = 0x80000000,
				.size = 256 * 1024 * 1024,
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
		},
		.versions = {
			{
				.intf = BCM2_INTF_BFC,
				.rwcode = 0x80002000,
			},
			{
				.version = "7.18.1009",
				.intf = BCM2_INTF_BFC,
				.magic = { 0x8070244c, "7.18.1009" },
				.options = {
					{ "bfc:conthread_instance", { 0x808bc084 }},
					{ "bfc:conthread_priv_off", { 0x6c }},
				}
			}
		},
	},
	{
		.name = "evw32c",
		.pretty = "Ubee EVW32C",
		.pssig = 0x1007,
		.blsig = 0x3384,
		.baudrate = 115200,
		.kseg1mask = 0x20000000,
		.magic = {
			{ 0x83f8e8a8, "1.0.03" },
		},
		.cfg_flags =
			BCM2_CFG_ENC_AES128_CBC | BCM2_CFG_FMT_GWS_FULL_ENC |
			BCM2_CFG_FMT_GWS_LEN_PREFIX | BCM2_CFG_PAD_PKCS7,
		.cfg_md5key = "3250736c633b752865676d64302d2778",
		.cfg_defkeys = {
			// key, followed by initialization vector
			"6c3ea0477630ce21a2ce334aa746c2cd" "c782dc4c098c66cbd9cd27d825682c81",
		},
		.spaces = {
			{
				.name = "ram",
				.min = 0x80000000,
				.size = 256 * 1024 * 1024,
			},
			{
				.name = "nvram",
				.size = 0x100000,
				.parts = {
					{ "bootloader", 0x00000, 0x10000 },
					{ "permnv",     0x10000, 0x20000, "perm" },
					{ "unknown",	0x30000, 0x90000 },
					{ "dynnv",      0xc0000, 0x40000, "dyn" },
				}
			},
			{
				.name = "flash",
				.size = 128 * 1024 * 1024,
				.parts = {
					{ "linuxapps", 0x0000000, 0x4c40000, "image3e" },
					{ "image1",    0x4c40000, 0x0d80000 },
					{ "image2",    0x59c0000, 0x0d80000 },
					{ "linux",     0x6740000, 0x0480000, "image3" },
					{ "linuxkfs",  0x6bc0000, 0x1200000, "" },
					{ "dhtml",     0x7dc0000, 0x0240000 },
				}
			},
		},
		.versions = {
			{
				.intf = BCM2_INTF_BFC,
				.options = {
					BCM2_VAL_STR("bfc:su_password", "ubeecable"),
					BCM2_VAL_U32("bfc:conthread_priv_off", 0x74),
					BCM2_VAL_U32("bfc:flash_reinit_on_retry", true),
					BCM2_VAL_U32("bfc:flash_read_direct", false)
				},
			},
			{
				.version = "2.7.1002",
				.intf = BCM2_INTF_BFC,
				.magic = { 0x810a4390, "2.7.1002-NCS" },
			},
		}
	},
	{
		.name = "epc3008",
		.pretty = "Cisco EPC3008",
		.baudrate = 115200,
		.pssig = 0xa03a,
		.blsig = 0xa03a,
		.arch = BCM2_3382,
		.kseg1mask = 0x20000000,
		.spaces = {
			{
				.name = "ram",
				.min = 0x80000000,
				.size = 64 * 1024 * 1024,
			},
			{
				.name = "flash",
				.size = 4 * 1024 * 1024,
				.parts = {
					{ "bootloader", 0x000000, 0x010000 },
					{ "permnv",     0x010000, 0x010000 },
					{ "image1",     0x020000, 0x1e0000 },
					{ "image2",     0x200000, 0x1e0000 },
					{ "dynnv",      0x3e0000, 0x020000 },
				},
			},
		},
		.versions = {
			{
				.intf = BCM2_INTF_BLDR,
				.rwcode = 0x80010000,
				.buffer = 0x82f00000,
			},
			{
				.intf = BCM2_INTF_BLDR,
				.version = "2.3.1_R3",
				.magic = { 0x83f8a18c, "2.3.1_R3 Pre-release" },
				.printf = 0x83f87b18,
				.sscanf = 0x83f884ec,
				.getline = 0x83f877c4,
				.spaces = {
					{
						.name = "flash",
						.read = { 0x83f80dc0, BCM2_READ_FUNC_OBL },
						.write = { 0x83f80c48, 0 },
						.erase = { 0x83f80fb0, BCM2_ERASE_FUNC_OL },
					}
				}
			},
		},
	},
	{
		.name = "tc8715",
		.pretty = "Technicolor TC8715",
		.baudrate = 115200,
		.pssig = 0xa8ef,
		.kseg1mask = 0x20000000,
		.spaces = {
			{
				.name = "ram",
				.min = 0x80000000
			},
			{
				.name = "nvram",
				.size = 0x100000,
				.parts = {
					{ "bootloader", 0x00000, 0x10000 },
					{ "permnv",     0x10000, 0x20000, "perm" },
					{ "eripv2",     0x30000, 0x20000, "" },
					{ "dynnv",      0xc0000, 0x40000, "dyn" }
				},
			},
			{
				.name = "flash",
				.size = 128 * 1024 * 1024,
				.parts = {
					{ "linuxapps", 0x0000000, 0x50c0000 },
					{ "image1",    0x50c0000, 0x0900000 },
					{ "image2",    0x59c0000, 0x0900000 },
					{ "linux",     0x62c0000, 0x0900000 },
					{ "linuxkfs",  0x6bc0000, 0x1200000 },
					{ "dhtml",     0x7dc0000, 0x0240000 },
				}
			},
		}
	},
	{
		.name = "tc7200",
		.pretty = "Technicolor TC7200",
		.arch = BCM2_3383,
		.baudrate = 115200,
		.pssig = 0xa825,
		.blsig = 0x3386,
		.kseg1mask = 0x20000000,
		.cfg_flags = BCM2_CFG_ENC_AES256_ECB | BCM2_CFG_PAD_ZEROBLK |
			BCM2_CFG_FMT_GWS_PAD_OPTIONAL,
		.cfg_md5key = "544d4d5f544337323030000000000000",
		.cfg_keyfun = &keyfun_tc7200,
		.cfg_defkeys = {
			"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
			"0001020304050607080910111213141516171819202122232425262728293031"
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
				.rwcode = 0x80002000,
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
						.write = { 0x83f82e98, 0 },
						.erase = { 0x83f82c08, BCM2_ERASE_FUNC_OS },
					},
					{
						.name = "nvram",
						.read = { 0x83f81298, BCM2_READ_FUNC_OBL },
						.write = { 0x83f810bc, 0 },
						.erase = { 0x83f814e0, BCM2_ERASE_FUNC_OL },
					}
				}
			},
			{
				.intf = BCM2_INTF_BFC,
				.rwcode = 0x80002000,
				.buffer = 0x85f00000,
				.buflen = 0x19c0000,
				.options = {
					BCM2_VAL_U32("bfc:conthread_priv_off", 0x74),
					BCM2_VAL_STR("bfc:su_password", "brcm"),
				},
			},
			{
				.version = "STD6.02.42",
				.intf = BCM2_INTF_BFC,
				.magic = { 0x814e953c, "STD6.02.42" },
				.spaces = {
						{
							.name = "flash",
							.open = { 0x803f72e4, BCM2_ARGS_OE },
							.read = { 0x803f6d90, BCM2_READ_FUNC_BOL,
									.patch = {{ 0x803f6f3c, 0x10000018 }},
							}
						},
				}
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
				}
			},
			{
				.version = "STD6.02.11",
				.intf = BCM2_INTF_BFC,
				.magic = { 0x85f00014, "TC7200U-D6.02.11" },
				.spaces = {
						{
							.name = "flash",
							.open = { 0x803e5fd4, BCM2_ARGS_OE },
							.read = { 0x803e5a80, BCM2_READ_FUNC_BOL,
									.patch = {{ 0x803e5c2c, 0x10000018 }},
							}
						},
				},
			},
			{
				.version = "STD6.01.27",
				.intf = BCM2_INTF_BFC,
				.magic = { 0x85f00014, "TC7200U-D6.01.27" },
				.spaces = {
						{
							.name = "flash",
							.open = { 0x8039eabc, BCM2_ARGS_OE },
							.read = { 0x8039e868, BCM2_READ_FUNC_BOL,
									.patch = {{ 0x8039e9bc, 0x10000018 }},
							}
						},
				},
				.options = {
					{ "bfc:conthread_instance", { 0x81315c24 }},
				},
			},
		},
	},
	// end marker
	{ .name = "" },
};

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
		.name = "generic",
		.pretty = "Generic Profile",
		.baudrate = 115200,
		.spaces = {
			{
				.name = "ram",
			},
		},
		.cfg_defkeys = {
			"0000000000000000000000000000000000000000000000000000000000000000",
		}
	},
	{
		.name = "TWG850",
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
			{
				.name = "flash",
				.min = 0xbf000000,
				.mem = true,
				.size = 8 * 1024 * 1024,
				.parts = {
					{ "image2",     0xbf000000, 0x3e0000 },
					{ "dynnv",      0xbf3e0000, 0x020000, "dyn" },
					{ "bootloader", 0xbf400000, 0x010000 },
					{ "image1",     0xbf410000, 0x3e0000 },
					{ "permnv",     0xbf7f0000, 0x010000, "perm" }
				}
			},

		}
	},
	{
		.name = "TWG870",
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
					{ "image1/2",   0x82f00000, 0x3e0000 },
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
		.name = "TC7200",
		.pretty = "Technicolor TC7200",
		.baudrate = 115200,
		.pssig = 0xa825,
		.blsig = 0x3386,
		.loadaddr = 0x84010000,
		.buffer = 0x85f00000,
		.kseg1mask = 0x20000000,
		.printf = 0x83f8b0c0,
		.scanf = 0x83f8ba94,
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
					{ "image1/2",   0x85f00000, 0x6c0000 },
					{ "linux",      0x87000000, 0x480000 }
				}
			},
			{
				.name = "nvram",
				.size = 0x100000,
				.parts = {
					{ "bootloader", 0x00000, 0x10000 },
					{ "permnv",     0x10000, 0x10000, "perm" },
					{ "dynnv",      0x20000, 0xe0000, "dyn" }
				},
				.read = {
					{
						.addr = 0x83f81298,
						.intf = BCM2_INTF_BLDR,
						.mode = BCM2_READ_FUNC_OBL,
					}
				},
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
				.read = {
					{
						.addr = 0x83f831b4,
						.intf = BCM2_INTF_BLDR,
						.mode = BCM2_READ_FUNC_BOL,
						.patch = {
							{ 0x83f83380, 0x10000017 },
						}
					}
				},
			},
		}
	},
	// end marker
	{ .name = "" },
};

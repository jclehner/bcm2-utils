#include <string.h>
#include "profile.h"

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
		.name = "tc7200",
		.pretty = "Technicolor TC-7200/TC-7200.U",
		.baudrate = 115200,
		.pssig = 0xa825,
		.loadaddr = 0x84010000,
		.buffer = 0x85f00000,
		.kseg1mask = 0x20000000,
		.printf = 0x83f8b0c0,
		.scanf = 0x83f8ba94,
		// not yet used
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
	}
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


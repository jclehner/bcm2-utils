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
 * along with nmrpflash.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef BCM2UTILS_NONVOL_H
#define BCM2UTILS_NONVOL_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

enum bcm2_nv_type {
	// invalid type
	BCM2_INVALID = 0,
	// custom type
	BCM2_CUSTOM = 1,
	// ascii string (size = max length)
	BCM2_ASTRING = 2,
	// string prefixed with byte
	BCM2_PSTRING = 3,
	// ipv4 address
	BCM2_IP4 = 4,
	// ipv6 address
	BCM2_IP6 = 5,
	// mac address
	BCM2_MAC = 6,
	// byte array
	BCM2_BYTEARR = 7,
	// 8 bit number
	BCM2_N8 = 8,
	// 16 bit number
	BCM2_N16 = 9,
	// 32 bit number
	BCM2_N32 = 10
};

union bcm2_nv_group_magic {
	uint32_t n;
	char s[4];
};

struct bcm2_nv_group {
	struct bcm2_nv_group *next;
	union bcm2_nv_group_magic magic;
	uint8_t version[2];
	const char *name;
	size_t offset;
	uint16_t size;
	bool invalid;
};

struct bcm2_nv_opt {
	struct bcm2_nv_opt *next;
	unsigned char *value;
	size_t size;
};

struct bcm2_nv_group *bcm2_nv_parse_groups(unsigned char *buf, size_t len, size_t *remaining);
void bcm2_nv_free_groups(struct bcm2_nv_group *groups);

#ifdef __cplusplus
}
#endif
#endif

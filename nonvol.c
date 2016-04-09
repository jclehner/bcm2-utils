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

#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include "nonvol.h"

struct nv_optdef {
	char name[64];
	char id[16];
	// type
	enum bcm2_nv_type type;
	// offset within group
	size_t offset;
	// size (only for some types)
	ssize_t size;
};

struct nv_groupdef {
	union bcm2_nv_group_magic magic;
	char name[64];
	char id[16];
	struct nv_optdef opts[64];
};

/*
 * In-flash group format:
 * length, including length field (2 bytes)
 * magic (4 bytes)
 * version (4 bytes, 2 major, 2 minor)
 */

static struct nv_groupdef groups[] = {
	{
		.magic.s = "UPC.",
		.name = "UPC Settings",
	},
	{
		.magic.s = "CMAp",
		.name = "BFC App Settings",
	},
	{
		.magic.s = "TR69",
		.name = "TR69 App Settings",
	},
	{
		.magic.s = "MSC.",
		.name = "Media Server App Settings",
	},
	{
		.magic.s = "NAS.",
		.name = "NAS App Settings",
	},
	{
		.magic.s = "THOM",
		.id = "thombfc",
		.name = "Thomson BFC Settings",
		.opts = {
			{
				.name = "Console mode",
				.id = "console",
				.type = BCM2_N8
			},
		}
	},
	{
		.magic.s = "TRG.",
		.name = "Thomson Residential Gateway Settings",
	},
	{
		.magic.s = "RG..",
		.name = "Broadcom Residential Gateway Settings",
	},
	{
		.magic.s = "CDP.",
		.name = "CDP Settings",
	},
	{
		.magic.s = "CAP.",
		.name = "CAP Settings",
	},
	{
		.magic.s = "bpi ",
		.name = "CM BPI Settings",
	},
	{
		.magic.s = "FIRE",
		.name = "Firewall Settings",
	},
	{
		.magic.s = "MLog",
		.name = "User Interface Settings",
		.id = "ui",
		.opts = {
			{
				.name = "HTTP username",
				.id = "http_user_id",
				.type = BCM2_PSTRING
			},
			{
				.name = "HTTP password",
				.id = "http_user_pw",
				.type = BCM2_PSTRING
			},
			{
				.name = "HTTP admin username",
				.id = "http_admin_id",
				.type = BCM2_PSTRING
			},
			{
				.name = "HTTP admin password",
				.id = "http_admin_pw",
				.type = BCM2_PSTRING
			},
		}
	},
	{
		.magic.s = "T802",
		.name = "Thomson Wi-Fi Settings",
	},
	{
		.magic.s = "8021",
		.name = "Broadcom Wi-Fi Settings",
	},
	{
		.magic.s = "WiGu",
		.name = "Broadcom Wi-Fi Guest Network Settings",
	},
	{
		.magic.s = "WFHH",
		.name = "Broadcom Wi-Fi Home Hotspot Settings",
	},
	{
		.magic.s = "802S",
		.name = "Broadcom Wi-Fi SPROM Settings",
	},
	{
		.magic.s = "FACT",
		.name = "Factory Settings",
	},
	{
		.magic.n = 0xd0c20100,
		.name = "CM DOCSIS Settings",
	},
	{
		.magic.n = 0xd0c20130,
		.name = "CM DOCSIS 3.0 Settings",
	},
	{
		.magic.s = "CMEV",
		.name = "CM Event Log",
	},
	{
		.magic.s = "DnSt",
		.name = "CM Downstream Calibration Settings",
	},
	{
		.magic.s = "UpSt",
		.name = "CM Upstream Calibration Settings",
	},
	{
		.magic.s = "PRNT",
		.name = "Parental Control Settings",
	},
	{
		.magic.s = "PPPS",
		.name = "PPP Settings",
	},
	{
		.magic.s = "VPNG",
		.name = "VPN Settings"
	},
	{
		.magic.s = "ERT.",
		.name = "eRouter Settings",
	},
	{
		.magic.s = "fwfr",
		.name = "eRouter Firewall Filter Settings",
	},
	{
		.magic.s = "v6fw",
		.name = "eRouter IPv6 Firewall Settings",
	},
	{
		.magic.n = 0x50530d56,
		.name = "ProgramStore Device Settings"
	},
	{
		.magic.n = 0xf2a1f61f,
		.name = "HAL Interface Settings"
	},
	{
		.magic.s = "AVS\0",
		.name = "AVS Management Settings"
	},
	{
		.magic.n = 0,
		.name = ""
	},
};

struct nv_groupdef *find_groupdef(union bcm2_nv_group_magic *m)
{
	struct nv_groupdef *groupdef = groups;

	for (; *groupdef->name; ++groupdef) {
		if (groupdef->magic.n == m->n || htonl(groupdef->magic.n) == m->n) {
			return groupdef;
		}
	}

	return NULL;
}

struct bcm2_nv_group *bcm2_nv_parse_groups(unsigned char *buf, size_t len, size_t *remaining)
{
	struct bcm2_nv_group *groups = NULL, *group;

	if (len < 6) {
		fprintf(stderr, "error: length %zu is less than 6\n", len);
		return NULL;
	}

	*remaining = len;
	groups = group = NULL;

	while (*remaining > 6) {
		if (groups) {
			group->next = malloc(sizeof(*group));
			group = group->next;
		} else {
			groups = group = malloc(sizeof(*group));
		}

		group->next = NULL;
		group->offset = len - *remaining;
		group->size = ntohs(*(uint16_t*)buf);
		memcpy(group->magic.s, buf + 2, 4);
		memcpy(group->version, buf + 6, 2);

		if (*remaining == 16 && !group->magic.n && !group->size) {
			char zeroblk[16] = { 0 };
			if (!memcmp(buf, zeroblk, 16)) {
				group->name = "(padding)";
				*remaining -= 16;
			}
			break;
		}

		struct nv_groupdef *def = find_groupdef(&group->magic);
		if (def) {
			group->name = def->name;
		} else {
			group->name = "(unknown)";
		}

		if (group->size > *remaining || group->size < 4) {
			group->name = "(invalid)";
			group->invalid = true;
			break;
		} else {
			group->invalid = false;
		}

		buf += group->size;
		*remaining -= group->size;
	}

	if (*remaining == 16) {
	}

	return groups;
}

void bcm2_nv_free_groups(struct bcm2_nv_group *groups)
{
	struct bcm2_nv_group *group = groups;
	for (; group; group = group->next) {
		free(group);
	}
}

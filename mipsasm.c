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
 * along with bcm2-utils.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <arpa/inet.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mipsasm.h"

#define MAX_LABEL_NUM 128

#define GET_OP(n) (((n) >> 26) & 0x3f)
#define GET_RX(n, x) ((n) >> (11 + (x * 5)) & 0x1f)
#define GET_RS(n) GET_RX(n, 2)
#define GET_RT(n) GET_RX(n, 1)

int mipsasm_resolve_labels(uint32_t *code, uint32_t *size, uint32_t offset)
{
	uint32_t i = offset;
	uint32_t labels[MAX_LABEL_NUM];
	memset(labels, 0xff, sizeof(labels));

	while (i < *size) {
		uint32_t opcode = ntohl(code[i / 4]);
		if (GET_OP(opcode) != ASM_LABEL_OPCODE 
				|| GET_RS(opcode) 
				|| GET_RT(opcode)
				|| !(opcode & ASM_LABEL_MARKER)) {
			i += 4;
			continue;
		}

		uint32_t label = opcode & ASM_LABEL_MASK;
		if (label >= MAX_LABEL_NUM) {
			fprintf(stderr, "%s: %02x: label %u exeeds %u\n", 
					__func__, i, label, MAX_LABEL_NUM);
			return 1;
		}

#ifdef MIPSASM_DEBUG
		printf("%s: %02x: label %u (opcode %08x)\n", __func__, i, label, opcode);
#endif

		labels[label] = i / 4;
		*size -= 4;
		uint32_t *dest = code + (i / 4);
		memmove(dest, dest + 1, *size - i);
	}

	for (i = offset; i < *size; i += 4) {
		uint32_t opcode = ntohl(code[i / 4]);
		uint32_t op = GET_OP(opcode);
		switch (op) {
			case 0x01:
			case 0x04:
			case 0x05:
			case 0x06:
			case 0x07:
				break;
			default:
				continue;
		}

		uint32_t imm = opcode & 0xffff;
		if (!(imm & ASM_LABEL_MARKER)) {
#ifdef MIPSASM_DEBUG
			printf("%s: %02x: skiping non-labeled branch to %02x\n", __func__, i, imm);
#endif
			// skip unlabeled branchp instruction
			continue;
		}

		imm &= ASM_LABEL_MASK;
		if (imm > MAX_LABEL_NUM) {
			fprintf(stderr, "%s: %02x: branch refers to out-of-range label %u\n",
					__func__, i, imm);
			return 1;
		}

		uint32_t instr = labels[imm];
		if (instr == SIZE_MAX) {
			fprintf(stderr, "%s: %02x: branch refers to undefined label %u\n",
					__func__, i, imm);
			return 1;
		}

		int32_t diff = (instr - (i / 4)) - 1;
		if (diff < INT16_MIN || diff > INT16_MAX) {
			fprintf(stderr, "%s: %02x: branch target %x out of range\n", __func__, i, diff);
			return 1;
		}
		opcode &= 0xffff0000;
		opcode |= (diff & 0xffff);
		code[i / 4] = htonl(opcode);
#ifdef MIPSASM_DEBUG
		printf("%s: %02x: branch to %02x\n", __func__, i, (diff * 4) & 0xffff);
#endif
	}

	return 0;
}

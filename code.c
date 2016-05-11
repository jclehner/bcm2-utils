/**
 * bcm2-utils - bcm2dump
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

#include "bcm2dump.h"
#include "mipsasm.h"

#define CODE_MAGIC 0xbeefc0de

#define L_LOOP_PATCH ASM_LABEL(0)
#define L_PATCH_DONE ASM_LABEL(1)
#define L_READ_FLASH ASM_LABEL(2)
#define L_LOOP_BZERO ASM_LABEL(3)
#define L_START_DUMP ASM_LABEL(4)
#define L_LOOP_LINE  ASM_LABEL(5)
#define L_LOOP_WORDS ASM_LABEL(6)

#define DATA_BEGIN 0x10
#define CODE_BEGIN 0x4c

static uint32_t dumpcode[] = {
		_WORD(CODE_MAGIC),
		// ":%x"
		_WORD(0x3a257800),
		// "\r\n"
		_WORD(0x0d0a0000),
		_WORD(0), // flags
		_WORD(0), // dump offset
		_WORD(0), // buffer
		_WORD(0), // offset
		_WORD(0), // length
		_WORD(0), // chunk size
		_WORD(0), // printf
		_WORD(0), // <flash read function>
		_WORD(0), // <patch offset 1>
		_WORD(0), // <patch word 1>
		_WORD(0), // <patch offset 2>
		_WORD(0), // <patch word 2>
		_WORD(0), // <patch offset 3>
		_WORD(0), // <patch word 3>
		_WORD(0), // <patch offset 4>
		_WORD(0), // <patch word 4>
		// main:
		ADDIU(SP, SP, -0x1c),
		SW(RA, 0x00, SP),
		SW(S7, 0x04, SP),
		SW(S4, 0x08, SP),
		SW(S3, 0x0c, SP),
		SW(S2, 0x10, SP),
		SW(S1, 0x14, SP),
		SW(S0, 0x18, SP),
		// branch to next instruction
		BAL(1),
		// delay slot: address mask
		LUI(T0, 0xffff),
		// store ra & 0xffff0000
		AND(S7, RA, T0),
		// buffer
		LW(S0, 0x14, S7),
		// offset
		LW(S1, 0x18, S7),
		// length
		LW(S2, 0x1c, S7),
		// dump offset
		LW(S3, 0x10, S7),
		// flash read function
		LW(S4, 0x28, S7),
		// branch to start_dump if we have a dump offset
		BNEZ(S3, L_START_DUMP),
		// delay slot: maximum of 4 words can be patched
		ORI(T0, ZERO, 4),

		// pointer to first patch blob
		ADDIU(T1, S7, 0x2c),
_DEF_LABEL(L_LOOP_PATCH),
		// load patch offset
		LW(T2, 0, T1),
		// break if patch offset is zero
		BEQ(T2, 0, L_PATCH_DONE),
		// delay slot: load patch word
		LW(T3, 4, T1),
		// patch word at t1
		SW(T3, 0, T2),
		// decrement counter
		ADDIU(T0, T0, -1),
		// loop until we've reached the end
		BGTZ(T0, L_LOOP_PATCH),
		// delay slot: set pointer to next patch blob
		ADDIU(T1, T1, 8),

_DEF_LABEL(L_PATCH_DONE),
		// if S4 is null, we're dumping RAM
		BNEZ(S4, L_READ_FLASH),
		// delay slot: load flags
		LW(V0, 0x0c, S7),
		// use memory offset as buffer
		MOVE(S0, S1),
		B(L_START_DUMP),
		// delay slot: store new buffer
		SW(S0, 0x14, S7),

_DEF_LABEL(L_READ_FLASH),
		// set t0 to buffer
		MOVE(T0, S0),
		// set t1 to length
		MOVE(T1, T2),

_DEF_LABEL(L_LOOP_BZERO),
		// zero word at t0
		SW(ZERO, 0, T0),
		// loop until T1 == 0
		ADDIU(T1, T1, -4),
		BGTZ(T1, L_LOOP_BZERO),
		// delay slot: increment buffer
		ADDIU(T0, T0, 4),

		// set t0 if dump function is (buffer, offset, length)
		ANDI(T0, V0, CODE_DUMP_PARAMS_BOL),
		// set t1 if dump dunfction is (offset, buffer, length)
		ANDI(T1, V0, CODE_DUMP_PARAMS_OBL),
		// set a0 = &buffer, a1 = offset, a2 = length
		ADDIU(A0, S7, 0x14),
		MOVE(A1, S1),
		MOVE(A2, S2),
		// if t0: set a0 = buffer
		MOVN(A0, S0, T0),
		// if t1: set a0 = offset and a1 = buffer
		MOVN(A0, S1, T1),
		MOVN(A1, S0, T1),
		// read from flash
		JALR(S4),
		// leave this here!
		NOP,

_DEF_LABEL(L_START_DUMP),
		// save s2 (remaining length)
		MOVE(T2, S2),
		// set s2 to MIN(remaining length, chunk size)
		LW(S2, 0x20, S7),
		SLT(T0, T2, S2),
		MOVN(S2, T2, T0),
		// increment buffer, offset and dump offset
		ADDU(S0, S0, S3),
		ADDU(S1, S1, S3),
		ADDU(S3, S3, S2),
		// store dump offset
		SW(S3, 0x10, S7),
		// load remaining length, decrement by s2, and store
		LW(T0, 0x1c, S7),
		SUBU(T0, T0, S2),
		SW(T0, 0x1c, S7),
		// set s4 to print function
		LW(S4, 0x24, S7),

_DEF_LABEL(L_LOOP_LINE),
		// 4 words per line
		ORI(S3, ZERO, 4),
		// load code offset
		MOVE(A0, S7),

_DEF_LABEL(L_LOOP_WORDS),
		// printf(":%x", *s0)
		ADDIU(A0, A0, 4),
		JALR(S4),
		LW(A1, 0, S0),
		// increment offset and buffer
		ADDIU(S0, S0, 4),
		ADDIU(S1, S1, 4),
		// decrement length and loop counter
		ADDI(S2, S2, -4),
		ADDI(S3, S3, -1),
		BGTZ(S3, L_LOOP_WORDS),
		// printf("\r\n")
		MOVE(A0, S7),
		JALR(S4),
		ADDIU(A0, A0, 0x8),
		// branch to loop_line if length > 0
		BGTZ(S2, L_LOOP_LINE),
		// delay slot
		NOP,
		// restore registers
		LW(RA, 0x00, SP),
		LW(S7, 0x04, SP),
		LW(S4, 0x08, SP),
		LW(S3, 0x0c, SP),
		LW(S2, 0x10, SP),
		LW(S1, 0x14, SP),
		LW(S0, 0x18, SP),
		JR(RA),
		ADDIU(SP, SP, 0x1c),
		// checksum
		_WORD(0)
};

static uint32_t calc_checksum(uint32_t *code, size_t size)
{
	uint32_t chk = 0;
	size_t i = 0;
	size /= 4;

	for (; i < size; ++i) {
		chk = (chk >> 1) + ((chk & 1) << 31);
		chk += code[i];
	}

	return chk;
}

static inline void patch_32(uint32_t *code, unsigned off, uint32_t val)
{
	char *p = (char*)code;
	val = htonl(val);
	memcpy(p + off, &val, 4);
}

static inline void patch_16(uint32_t *code, unsigned off, uint16_t val)
{
	char *p = (char*)code;
	val = htons(val);
	memcpy(p + off, &val, 2);
}

bool code_init_and_upload(int fd, struct code_cfg *cfg, code_upload_callback callback, void *arg)
{
	uint32_t codeaddr = cfg->profile->loadaddr | cfg->profile->kseg1mask;

	if (!cfg->code) {
		cfg->profile->buffer |= cfg->profile->kseg1mask;
		cfg->profile->loadaddr |= cfg->profile->kseg1mask;

		patch_32(dumpcode, 0x10, 0);
		patch_32(dumpcode, 0x14, cfg->profile->buffer);
		patch_32(dumpcode, 0x18, cfg->offset);
		patch_32(dumpcode, 0x1c, cfg->length);
		patch_32(dumpcode, 0x20, cfg->chunklen);
		patch_32(dumpcode, 0x24, cfg->profile->printf);

		struct bcm2_func *read = &cfg->addrspace->read;
		patch_32(dumpcode, 0x0c, read->mode);
		patch_32(dumpcode, 0x28, read->addr);

		unsigned i = 0;
		for (; i < BCM2_PATCH_NUM; ++i) {
			uint32_t offset = 0x2c + (8 * i);
			uint32_t addr = cfg->nopatch ? 0 : read->patch[i].addr;
			uint32_t word = cfg->nopatch ? 0 : read->patch[i].word;

			if (addr) {
				addr |= cfg->profile->kseg1mask;
			}

			patch_32(dumpcode, offset, addr);
			patch_32(dumpcode, offset + 4, word);
		}

		cfg->codesize = sizeof(dumpcode);
		if (mipsasm_resolve_labels(dumpcode, &cfg->codesize, CODE_BEGIN) != 0) {
			return false;
		}

		cfg->code = dumpcode;
		cfg->entry = CODE_BEGIN;

#if 0
		FILE *fp = fopen("dumpcode.bin", "w");
		fwrite(cfg->code, cfg->codesize, 1, fp);
		fclose(fp);
#endif

		uint32_t actual;
		uint32_t expected = calc_checksum(cfg->code + CODE_BEGIN,
				cfg->codesize - CODE_BEGIN - 4);
		if (!bl_read(fd, codeaddr + cfg->codesize - 4, &actual, 4)) {
			return false;
		}

		patch_32(cfg->code, cfg->codesize - 4, expected);

		if (ntohl(actual) == expected) {
			char data[CODE_BEGIN - DATA_BEGIN];
			if (!bl_read(fd, codeaddr + DATA_BEGIN, data, sizeof(data))) {
				return false;
			}

			struct progress prog;
			progress_init(&prog, codeaddr + DATA_BEGIN, sizeof(data));
			unsigned i = 0;
			for (; i < sizeof(data); i += 4) {
				uint32_t off = DATA_BEGIN + i;
				uint32_t *codedata = cfg->code + (off / 4);
				if (memcmp(data + i, codedata, 4)) {
					if (!bl_writew(fd, codeaddr + off, (const char*)codedata)) {
						return false;
					}
				}

				if (callback) {
					progress_add(&prog, 4);
					callback(&prog, false, arg);
				}
			}

			return true;
		}
	}

	// since reading is around 50x faster than writing, update only
	// those parts that really need updating

	char *ramcode = malloc(cfg->codesize);
	if (!ramcode) {
		perror("malloc");
		return false;
	}

	if (!bl_read(fd, codeaddr, ramcode, cfg->codesize)) {
		free(ramcode);
		return false;
	}

	struct progress prog;
	progress_init(&prog, codeaddr, cfg->codesize);
	size_t i = 0;
	for (; i < cfg->codesize; i += 4) {
		char *opcode = ((char*)cfg->code) + i;
		if (memcmp(ramcode + i, opcode, 4)) {
			if (!bl_writew(fd, codeaddr + i, opcode)) {
				free(ramcode);
				return false;
			}
		}

		if (callback) {
			progress_add(&prog, 4);
			callback(&prog, true, arg);
		}
	}

	free(ramcode);

	return true;
}

bool code_run(int fd, struct code_cfg *cfg)
{
	return bl_jump(fd, cfg->profile->loadaddr + cfg->entry);
}

bool code_parse_values(const char *line, uint32_t *buf, bool *parseable)
{
	// all lines are prefixed with ':' to distinguish them from
	// messages generated by the bootloader code
	*parseable = (line[0] == ':');
	if (*parseable) {
		if (sscanf(line, ":%x:%x:%x:%x", buf, buf + 1, buf + 2, buf + 3) != 4) {
			return false;
		}

		return true;
	}

	return false;
}

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

#include "mipsasm.h"
#include "profile.h"

#define CODE_MAGIC 0xbeefc0de
//#define WRITECODE_PRINT_OFFSETS

#define WRITECODE_ENTRY (0x28 + 0x0c)
#define WRITECODE_STRSIZE 0x28
#define WRITECODE_STACKSIZE (0x20 + WRITECODE_STRSIZE)
#define WRITECODE_STROFF (WRITECODE_STACKSIZE - WRITECODE_STRSIZE)
#define WRITECODE_CFGOFF (0x0c + 0x0c)

#define L_SCANF      ASM_LABEL(0)
#define L_WORD_OK    ASM_LABEL(1)
// XXX: these are reused below; do NOT change!
#define L_LOOP_LINE  ASM_LABEL(5)
#define L_LOOP_WORDS ASM_LABEL(6)
#define L_OUT        ASM_LABEL(7)

uint32_t writecode[] = {
		_WORD(CODE_MAGIC),
		// ":%x:"
		_WORD(0x3a25783a),
		// "%x:%"
		_WORD(0x25783a25),
		// "x:%x"
		_WORD(0x783a2578),
		// ":%x"
		_WORD(0x3a257800),
		// "\r\n"
		_WORD(0x0d0a0000),
		_WORD(0), // flags
		_WORD(0), // buffer
		_WORD(0), // length
		_WORD(0), // chunk size
		_WORD(0), // printf
		_WORD(0), // scanf / sscanf
		_WORD(0), // getline
		// main:
		ADDIU(SP, SP, -WRITECODE_STACKSIZE),
		SW(RA, 0x00, SP),
		SW(S7, 0x04, SP),
		SW(S0, 0x08, SP),
		SW(S1, 0x0c, SP),
		SW(S2, 0x10, SP),
		SW(S3, 0x14, SP),
		SW(S4, 0x18, SP),
		SW(S5, 0x1c, SP),

		// branch to next instruction
		BAL(1),
		// delay slot: address mask
		LUI(T0, 0xffff),
		// store ra & 0xffff0000
		AND(S7, RA, T0),
		// buffer
		LW(S0, WRITECODE_CFGOFF + 0x04, S7),
		// length
		LW(S1, WRITECODE_CFGOFF + 0x08, S7),
		// chunk size
		LW(S2, WRITECODE_CFGOFF + 0x0c, S7),
		// printf
		LW(S3, WRITECODE_CFGOFF + 0x10, S7),
		// scanf / sscanf
		LW(S4, WRITECODE_CFGOFF + 0x14, S7),
		// getline
		LW(S5, WRITECODE_CFGOFF + 0x18, S7),

		// bail out if length is zero
		BEQZ(S1, L_OUT),

		// set s2 to MIN(length, chunk_size)
		SLT(T0, S1, S2),
		MOVN(S2, S1, T0),

		// subtract chunk size from length
		SUBU(S1, S1, S2),

		// make sure that we have a NUL byte
		SB(ZERO, WRITECODE_STROFF + WRITECODE_STRSIZE - 1, SP),

_DEF_LABEL(L_LOOP_WORDS),
		// if getline is zero, we have a true scanf
		BEQZ(S5, L_SCANF),

		// delay slot: set first byte of string to zero
		SB(ZERO, WRITECODE_STROFF, SP),

		// getline(string, size)
		ADDIU(A0, SP, WRITECODE_STROFF),
		JALR(S5),
		ORI(A1, ZERO, WRITECODE_STRSIZE - 1),

#if 0
		// load flags
		LW(T1, WRITECODE_CFGOFF + 0x00, S7),
		ANDI(T0, T1, BCM2_FGETS_RET_BUF_PLUS_LEN),

		// if FGETS_RET_BUF_PLUS_LEN is set, set t0 to sp,
		// otherwise leave it at zero
		MOVN(T0, SP, T0),

		// this is a nop if FGETS_RET_BUF_PLUS_LEN is not set
		SUBU(V0, V0, T0),

		// set v0 to (flags & FGETS_RET_VOID), if applicable
		ANDI(T0, T1, BCM2_FGETS_RET_VOID),
		MOVN(V0, T0, T0),
		// bail out if fgets returned < 1
		SLTIU(V1, V0, 1),
		BNEZ(V1, L_OUT),
#endif
		// bail out if first byte in string is zero
		LBU(V1, WRITECODE_STROFF, SP),
		BEQZ(V1, L_OUT),

		// delay slot: string
		ADDIU(A0, SP, WRITECODE_STROFF),
		// sscanf(string, ":%x:%x:%x:%x", buffer, buffer + 4, buffer + 8, buffer + 12)
		ADDIU(A1, S7, 4),
		MOVE(A2, S0),
		ADDIU(A3, S0, 4),
		ADDIU(A4, S0, 8),
		JALR(S4),
		ADDIU(A5, S0, 12),

		B(L_WORD_OK),

_DEF_LABEL(L_SCANF),
		// delay slot: format string
		ADDIU(A0, S7, 4),
		// scanf(":%x:%x:%x:%x", buffer)
		MOVE(A1, S0),
		ADDIU(A2, S0, 4),
		ADDIU(A3, S0, 8),
		JALR(S4),
		ADDIU(A4, S0, 12),

		// bail out if scanf returned < 4
		SLTIU(V1, V0, 4),
		BNEZ(V1, L_OUT),

_DEF_LABEL(L_WORD_OK),
		// delay slot: format string (":%x")
		ADDIU(A0, S7, 16),
		// printf("%x", buffer)
		JALR(S3),
		MOVE(A1, S0),

		// printf("\r\n")
		JALR(S3),
		ADDIU(A0, S7, 20),

		// decrement length
		ADDIU(S2, S2, -16),
		// loop while length > 0
		BGTZ(S2, L_LOOP_WORDS),
		// delay slot: increment buffer
		ADDIU(S0, S0, 16),

#if 0
		// flash write function
		LW(S4, 0, ZERO),
		BEQZ(S4, L_WRITE_OK),
		// delay slot: flash erase function
		LW(S5, 0, ZERO),
		BEQZ(S5, L_WRITE),
#endif

		// store length and buffer
		SW(S0, WRITECODE_CFGOFF + 0x04, S7),
		SW(S1, WRITECODE_CFGOFF + 0x08, S7),

_DEF_LABEL(L_OUT),
		// restore registers
		LW(RA, 0x00, SP),
		LW(S7, 0x04, SP),
		LW(S0, 0x08, SP),
		LW(S1, 0x0c, SP),
		LW(S2, 0x10, SP),
		LW(S3, 0x14, SP),
		LW(S4, 0x18, SP),
		LW(S5, 0x1c, SP),
		JR(RA),
		ADDIU(SP, SP, WRITECODE_STACKSIZE),

		// checksum
		_WORD(0),
};

#define L_LOOP_PATCH ASM_LABEL(0)
#define L_PATCH_DONE ASM_LABEL(1)
#define L_READ_FLASH ASM_LABEL(2)
#define L_LOOP_BZERO ASM_LABEL(3)
#define L_START_DUMP ASM_LABEL(4)
// labels 5-7 are reused from writecode
#define F_PATCH      ASM_LABEL(8)

#define DUMPCODE_ENTRY 0x4c

uint32_t dumpcode[] = {
		_WORD(CODE_MAGIC),
		// ":%x"
		_WORD(0x3a257800),
		// "\r\n"
		_WORD(0x0d0a0000),
		_WORD(0), // flags
		_WORD(0), // dump offset
		_WORD(0), // buffer
		_WORD(0), // offset (used when dumping flash)
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
		// bail out if length is zero
		BEQZ(S2, L_OUT),
		// delay slot: dump offset
		LW(S3, 0x10, S7),
		// branch to start_dump if we have a dump offset
		BNEZ(S3, L_START_DUMP),
		// delay slot: flash read function
		LW(S4, 0x28, S7),

		// patch code (affects only t0-t3)
		BAL(F_PATCH),
		NOP,

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
		ANDI(T0, V0, BCM2_READ_FUNC_BOL),
		// set t1 if dump dunfction is (offset, buffer, length)
		ANDI(T1, V0, BCM2_READ_FUNC_OBL),
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
_DEF_LABEL(L_OUT),
		// restore code
		BAL(F_PATCH),
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

_DEF_LABEL(F_PATCH),
		// maximum of 4 words can be patched
		ORI(V0, ZERO, 4),
		// pointer to first patch blob
		ADDIU(V1, S7, 0x2c),
_DEF_LABEL(L_LOOP_PATCH),
		// load patch offset
		LW(A0, 0, V1),
		// break if patch offset is zero
		BEQZ(A0, L_PATCH_DONE),
		// delay slot: load patch word
		LW(T0, 4, V1),
		// load current word at offset
		LW(T1, 0, A0),
		// patch word at offset
		SW(T0, 0, A0),
		// store original word in patch (this way, calling this
		// function again will restore the original code)
		SW(T1, 4, V1),
		// decrement counter
		ADDIU(V0, V0, -1),
		// loop until we've reached the end
		BGTZ(V0, L_LOOP_PATCH),
		// delay slot: set pointer to next patch blob
		ADDIU(T1, T1, 8),
_DEF_LABEL(L_PATCH_DONE),
		JR(RA),
		NOP,

		// checksum
		_WORD(0)
};

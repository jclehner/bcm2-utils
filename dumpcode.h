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

#define CODE_MAGIC 0xbeefc0de
//#define WRITECODE_PRINT_OFFSETS

#define WRITECODE_ENTRY 0x20

#define L_LOOP_WORDS ASM_LABEL(6)
#define L_OUT        ASM_LABEL(7)

uint32_t writecode[] = {
		_WORD(CODE_MAGIC),
		// ":%x"
		_WORD(0x3a257800),
		// "\r\n"
		_WORD(0x0d0a0000),
		_WORD(0), // buffer
		_WORD(0), // length
		_WORD(0), // chunk size
		_WORD(0), // printf
		_WORD(0), // scanf
		_WORD(0), // flags
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
		LW(S0, 0x10, S7),
		// length
		LW(S1, 0x14, S7),
		// chunk size
		LW(S2, 0x18, S7),
		// printf
		LW(S3, 0x1c, S7),
		// scanf
		LW(S4, 0x20, S7),

		// bail out if length is zero
		BEQZ(S1, L_OUT),

		// set s1 to MIN(length, chunk_size)
		MOVE(T1, S1),
		MOVE(S1, S2),
		SLT(T0, T1, S1),
		MOVN(S1, T1, T0),

_DEF_LABEL(L_LOOP_WORDS),
#ifdef WRITECODE_PRINT_OFFSETS
		// printf(":%x", buffer)
		ADDIU(A0, S7, 4),
		JR(S3),
		MOVE(A1, S0),
#endif

		// scanf("%x", buffer)
		ADDIU(A0, S7, 5),
		JR(S4),
		MOVE(A1, S0),

		// bail out if scanf didn't return 1
		BNE(V0, 1, L_OUT),
		// delay slot: decrement length
		ADDIU(S1, S1, -4),

		// loop while length > 0
		BGTZ(S1, L_LOOP_WORDS),
		// delay slot: increment buffer
		ADDIU(S0, S0, 4),

		// store length and buffer
		SW(S0, 0x10, S7),
		SW(S1, 0x14, S7),

_DEF_LABEL(L_OUT),
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
		_WORD(0),
};

#define L_LOOP_PATCH ASM_LABEL(0)
#define L_PATCH_DONE ASM_LABEL(1)
#define L_READ_FLASH ASM_LABEL(2)
#define L_LOOP_BZERO ASM_LABEL(3)
#define L_START_DUMP ASM_LABEL(4)
#define L_LOOP_LINE  ASM_LABEL(5)
// labels 6 and 7 are defined above
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
		// maximum of 4 words can be patched
		ORI(T0, ZERO, 4),

		// pointer to first patch blob
		ADDIU(T1, S7, 0x2c),
_DEF_LABEL(L_LOOP_PATCH),
		// load patch offset
		LW(T2, 0, T1),
		// break if patch offset is zero
		BEQZ(T2, L_PATCH_DONE),
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
_DEF_LABEL(L_OUT),
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

#define L_LOOP ASM_LABEL(0)
#define L_B2 ASM_LABEL(8)
#define L_B3 ASM_LABEL(9)

uint32_t base64code[] = {
		// S0 = in buffer
		// T1 = out buffer
		// S2 = length
		// T3 = base64 chars

_DEF_LABEL(L_LOOP),
		// out[0] = b64[in[0] >> 2]

		LB(V0, 0, S0),
		SRL(V0, V0, 2),

		ADDIU(V0, V0, T3),
		LB(V0, 0, V0),
		SW(V0, 0, T1),

		// out[1] = b64[((in[0] & 0x03) << 4) | (in[1] & 0xf0) >> 4)]

		LB(V0, 0, S0),
		ANDI(V0, V0, 0x03),
		SLL(V0, V0, 4),

		LB(V1, 1, S0),
		ANDI(V1, V1, 0xf0),
		SRL(V1, V1, 4),

		OR(V0, V0, V1),

		ADDIU(V0, V0, T3),
		LB(V0, 0, V0),
		SW(V0, 1, T1),

		// out[2] = (len > 1 ? b64[ ((in[1] & 0x0f) << 2) | (in[2] & 0xc0) >> 6 ] : '=')

		SLTIU(V1, S2, 2),
		BEQZ(V1, L_B2),
		ORI(V0, ZERO, '='),

		LB(V0, 1, S0),
		ANDI(V0, V0, 0x0f),
		SLL(V0, V0, 2),

		LB(V1, 2, S0),
		ANDI(V1, V1, 0xc0),
		SRL(V1, V1, 6),

		OR(V0, V0, V1),

		ADDIU(V0, V0, T3),
		LB(V0, 0, V0),

_DEF_LABEL(L_B2),
		SW(V0, 2, T1),

		// out[3] = (len > 2 ? b64[in[2] & 0x3f] : '=');

		SLTIU(V1, S2, 3),
		BEQZ(V1, L_B3),
		ORI(V0, ZERO, '='),

		LB(V0, 2, S0),
		ANDI(V0, V0, 0x3f),

		ADDIU(V0, V0, T3),
		LB(V0, 0, V0),

_DEF_LABEL(L_B3),
		SW(V0, 2, T1)
};

uint32_t dumpcode_b64[] = {
		_WORD(CODE_MAGIC),
		// "%s"
		_WORD(0x25730000),
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
		ADDIU(SP, SP, -0x22),
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
		// maximum of 4 words can be patched
		ORI(T0, ZERO, 4),

		// pointer to first patch blob
		ADDIU(T1, S7, 0x2c),
_DEF_LABEL(L_LOOP_PATCH),
		// load patch offset
		LW(T2, 0, T1),
		// break if patch offset is zero
		BEQZ(T2, L_PATCH_DONE),
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
_DEF_LABEL(L_OUT),
		// restore registers
		LW(RA, 0x00, SP),
		LW(S7, 0x04, SP),
		LW(S4, 0x08, SP),
		LW(S3, 0x0c, SP),
		LW(S2, 0x10, SP),
		LW(S1, 0x14, SP),
		LW(S0, 0x18, SP),
		JR(RA),
		ADDIU(SP, SP, 0x22),
		// checksum
		_WORD(0)
};

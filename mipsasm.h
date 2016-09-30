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

#ifndef MIPSASM_H
#define MIPSASM_H

#define ASM_OPCODE(x) \
	((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >> 8) | \
	(((x) & 0x0000ff00) << 8) | (((x) & 0x000000ff) << 24))

#define ASM_X_RX(n, x) (((n) & 0x1f) << (11 + ((x) * 5)))
#define ASM_X_RS(n) ASM_X_RX(n, 2)
#define ASM_X_RT(n) ASM_X_RX(n, 1)
#define ASM_R_RD(n) ASM_X_RX(n, 0)
#define ASM_X_OP(n) (((n) & 0x3f) << 26)
#define ASM_R_FN(n) ((n) & 0x3f)
#define ASM_R_SA(n) (((n) & 0x1f) << 6)
#define ASM_J_ADDR(n) (((n) & 0x0fffffff) >> 2)
#define ASM_HI(n)   (((n) & 0xffff0000) >> 16)
#define ASM_LO(n)   ((n) & 0xffff)

// R-type: zero(6) | rs(5) | rt(5) | rd(5) | sa(5) | fn(6)
#define ASM_R(rs, rt, rd, sa, fn) \
	ASM_OPCODE(ASM_X_RS(rs) | ASM_X_RT(rt) | ASM_R_RD(rd) | ASM_R_SA(sa) | ASM_R_FN(fn))

// I-type: op(6) | rs(5) | rt(5) | imm(16)
#define ASM_I(op, rs, rt, imm) \
	ASM_OPCODE(ASM_X_OP(op) | ASM_X_RS(rs) | ASM_X_RT(rt) | ((imm) & 0xffff))

// J-type: op(6) | target(26)
#define ASM_J(op, target) \
	ASM_OPCODE(ASM_X_OP(op) | (target & 0x3ffffff))

#define R(n) ((n) & 0x1f)
#define V(n) R(2 + n)
#define A(n) R(4 + n)
#define T(n) R(8 + n)
#define S(n) R(16 + n)

#define RA R(31)
#define SP R(29)
#define AT R(1)
#define ZERO R(0)

#define V0 V(0)
#define V1 V(1)

#define A0 A(0)
#define A1 A(1)
#define A2 A(2)
#define A3 A(3)
#define A4 T0
#define A5 T1
#define A6 T2
#define A7 T3

#define T0 T(0)
#define T1 T(1)
#define T2 T(2)
#define T3 T(3)
#define T4 T(4)
#define T5 T(5)
#define T6 T(6)
#define T7 T(7)

#define S0 S(0)
#define S1 S(1)
#define S2 S(2)
#define S3 S(3)
#define S4 S(4)
#define S5 S(5)
#define S7 S(7)

#define ADDIU(rt, rs, imm)   ASM_I(0x09, rs, rt, imm)
#define ADDI(rt, rs, imm)    ASM_I(0x08, rs, rt, imm)
#define ADDU(rd, rs, rt)     ASM_R(rs, rt, rd, 0, 0x21)
#define AND(rd, rs, rt)      ASM_R(rs, rt, rd, 0, 0x24)
#define ANDI(rt, rs, imm)    ASM_I(0x0c, rs, rt, imm)
#define B(target)            BEQ(ZERO, ZERO, target)
#define BEQ(rs, rt, target)  ASM_I(0x04, rs, rt, target)
#define BEQZ(rs, target)     BEQ(rs, ZERO, target)
#define BGTZ(rs, target)     ASM_I(0x07, rs, 0, target)
#define BLT(rs, rt, target)  SLT(AT, rs, rt), BNE(AT, ZERO, target)
#define BNE(rs, rt, target)  ASM_I(0x05, rs, rt, target)
#define BNEZ(rs, target)     BNE(rs, 0, target)
#define J(addr)              ASM_J(0x02, ASM_J_ADDR(addr))
#define JAL(addr)            ASM_J(0x03, ASM_J_ADDR(addr))
#define JALR(rs)             ASM_R(rs, 0, RA, 0, 0x09)
#define JR(rs)               ASM_R(rs, 0, 0, 0, 0x08)
#define LB(rt, imm, rs)      ASM_I(0x20, rs, rt, imm)
#define LBU(rt, imm, rs)     ASM_I(0x24, rs, rt, imm)
#define LUI(rt, imm)         ASM_I(0x0f, 0, rt, imm)
#define LI(rt, imm32)        LUI(rt, ASM_HI(imm32)), ORI(rt, rt, ASM_LO(imm32))
#define LW(rt, imm, rs)      ASM_I(0x23, rs, rt, imm)
#define MOVE(rt, rs)         ADDU(rt, rs, ZERO)
#define NOP                  0x00000000
#define SLL(rd, rt, sa)      ASM_R(0, rt, rd, sa, 0x00)
#define SRL(rd, rt, sa)      ASM_R(0, rt, rd, sa, 0x02)
#define OR(rd, rs, rt)       ASM_R(rs, rt, rd, 0, 0x25)
#define ORI(rt, rs, imm)     ASM_I(0x0d, rs, rt, imm)
#define SLT(rd, rs, rt)      ASM_R(rs, rt, rd, 0, 0x2a)
#define SLTIU(rt, rs, imm)   ASM_I(0x0b, rs, rt, imm)
#define SUBU(rd, rs, rt)     ASM_R(rs, rt, rd, 0, 0x23)
#define SB(rt, imm, rs)      ASM_I(0x28, rs, rt, imm)
#define SW(rt, imm, rs)      ASM_I(0x2b, rs, rt, imm)

#define BAL(target)          BGEZAL(ZERO, target)
#define BGEZAL(rs, target)   ASM_I(0x01, rs, 0x11, target)
#define MOVZ(rd, rs, rt)     ASM_R(rs, rt, rd, 0, 0x0a)
#define MOVN(rd, rs, rt)     ASM_R(rs, rt, rd, 0, 0x0b)

#define _WORD(n)             ASM_OPCODE(n)

#define ASM_LABEL_OPCODE 0x09
#define ASM_LABEL_MASK 0x7fff
#define ASM_LABEL_MARKER 0x8000

/* a nop instruction which carries a 15 bit label identifier (will be removed from
 * final code by mipsasm_resolve_labels)
 */
#define _DEF_LABEL(n) ASM_I(ASM_LABEL_OPCODE, ZERO, ZERO, \
		(ASM_LABEL(n)))
/* use this to define a label */
#define ASM_LABEL(n) (ASM_LABEL_MARKER | ((n) & ASM_LABEL_MASK))

#ifdef __cplusplus
extern "C" {
#endif

int mipsasm_resolve_labels(uint32_t *code, uint32_t *size, uint32_t offset);

#ifdef __cplusplus
}
#endif

#endif

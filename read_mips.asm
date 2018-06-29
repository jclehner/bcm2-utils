/**
 * bcm2-utils
 * Copyright (C) 2016-2018 Joseph Lehner <joseph.c.lehner@gmail.com>
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

#include "asmdef.h"

.set noreorder
.set mips4
.text

	.word BCM2_RWCODE_MAGIC
str_fmt:
	.ascii ":%x\0"
str_nl:
	.ascii "\r\n\0\0"

arg_flags:
	.word 0 // flags
arg_dumpoff:
	.word 0 // dump from this offset when code is called
arg_buffer:
	.word 0 // buffer
arg_flashoff:
	.word 0 // offset for flash read function
arg_length:
	.word 0 // length
arg_chunklen:
	.word 0 // chunk size
arg_printf:
	.word 0 // printf
arg_flashrd:
	.word 0 // flash read function
arg_patches:
	.word 0 // <patch offset 1>
	.word 0 // <patch word 1>
	.word 0 // <patch offset 2>
	.word 0 // <patch word 2>
	.word 0 // <patch offset 3>
	.word 0 // <patch word 3>
	.word 0 // <patch offset 4>
	.word 0 // <patch word 4>
main:
	addiu $sp, -0x1c
	sw $ra, 0x00($sp)
	sw $s7, 0x04($sp)
	sw $s4, 0x08($sp)
	sw $s3, 0x0c($sp)
	sw $s2, 0x10($sp)
	sw $s1, 0x14($sp)
	sw $s0, 0x18($sp)
	// branch to next instruction
	bal .next
	// delay slot: address mask
	lui $t4, 0xffff
.next:
	// store ra & 0xffff0000
	and $s7, $ra, $t4
	// length
	lw $s2, 0x1c($s7)
	// bail out if length is zero
	beqz $s2, .out
	// delay slot: offset
	lw $s1, %lo(arg_flashoff)($s7)
	// dump offset
	lw $s3, %lo(arg_dumpoff)($s7)
	// branch to dump if we have a dump offset
	bnez $s3, .dump
	// delay slot: buffer
	lw $s0, %lo(arg_buffer)($s7)

	// flash read function
	lw $s4, %lo(arg_flashrd)($s7)

	// if $s4 is null, we're dumping RAM
	bnez $s4, .read
	// delay slot: load flags
	lw $v0, %lo(arg_flags)($s7)
	// use memory offset as buffer
	move $s0, $s1
	b .dump
	// delay slot: store new buffer
	sw $s0, %lo(arg_buffer)($s7)

.read:
	// set t4 to buffer
	move $t4, $s0
	// set t5 to length
	move $t5, $s2

.bzero:
	// zero word at t4
	sw $zero, 0($t4)
	// loop until $t5 == 0
	addiu $t5, -4
	bgtz $t5, .bzero
	// delay slot: increment buffer
	addiu $t4, 4

	// patch code (clobbers t4-t8 only)
	bal f_patch

	// delay slot: a0 = &buffer
	addiu $a0, $s7, arg_buffer
	// a1 = offset
	move $a1, $s1
	// set t4 if dump function is (buffer, offset, length)
	andi $t4, $v0, BCM2_READ_FUNC_BOL
	// set t5 if dump dunfction is (offset, buffer, length)
	andi $t5, $v0, BCM2_READ_FUNC_OBL
	// if t4: set a0 = buffer
	movn $a0, $s0, $t4
	// if t5: set a0 = offset and a1 = buffer
	movn $a0, $s1, $t5
	movn $a1, $s0, $t5
	// read from flash
	jalr $s4
	// a2 = length
	move $a2, $s2

	// revert patch (clobbers t4-t8 only)
	bal f_patch

.dump:
	// delay slot: save s2 (remaining length)
	move $v0, $s2
	// set s2 to MIN(remaining length, chunk size)
	lw $s2, %lo(arg_chunklen)($s7)
	slt $t4, $v0, $s2
	movn $s2, $v0, $t4
	// increment buffer, offset and dump offset
	addu $s0, $s3
	addu $s1, $s3
	addu $s3, $s2
	// store dump offset
	sw $s3, %lo(arg_dumpoff)($s7)
	// load remaining length, decrement by s2, and store
	lw $t4, %lo(arg_length)($s7)
	subu $t4, $s2
	sw $t4, %lo(arg_length)($s7)
	// set s4 to print function
	lw $s4, %lo(arg_printf)($s7)

1:
	// 4 words per line
	ori $s3, $zero, 4
	// load code offset
	move $a0, $s7

2:
	// printf(":%x", *s0)
	addiu $a0, 4
	jalr $s4
	lw $a1, 0($s0)
	// increment offset and buffer
	addiu $s0, 4
	addiu $s1, 4
	// decrement length and loop counter
	addi $s2, -4
	addi $s3, -1
	bgtz $s3, 2b
	// printf("\r\n")
	move $a0, $s7
	jalr $s4
	addiu $a0, 8
	// branch to loop_line if length > 0
	bgtz $s2, 1b
	// delay slot
	nop
.out:
	// restore registers
	lw $ra, 0x00($sp)
	lw $s7, 0x04($sp)
	lw $s4, 0x08($sp)
	lw $s3, 0x0c($sp)
	lw $s2, 0x10($sp)
	lw $s1, 0x14($sp)
	lw $s0, 0x18($sp)
	jr $ra
	addiu $sp, 0x1c

f_patch:
	// maximum of 4 words can be patched
	ori $t6, $zero, 4
	// pointer to first patch blob
	addiu $t7, $s7, arg_patches
1:
	// load patch offset
	lw $t8, 0($t7)
	// break if patch offset is zero
	beqz $t8, 2f
	// delay slot: load patch word
	lw $t4, 4($t7)
	// load current word at offset
	lw $t5, 0($t8)
	// patch word at offset
	sw $t4, 0($t8)
	// store original word in patch (this way, calling this
	// function again will restore the original code)
	sw $t5, 4($t7)
	// decrement counter
	addiu $t6, -1
	// loop until we've reached the end
	bgtz $t6, 1b
	// delay slot: set pointer to next patch blob
	addiu $t7, 8
2:
	jr $ra
	nop

	// checksum
	.word 0

#include "asmdef.h"
#define STACK	0x1c

.set noreorder
.set mips4

.text

.word 0xbeefc0de

str_fmt:		.ascii ":%x\0"
str_crlf:		.ascii "\r\n\0\0"

cfg:

cfg_flags:		.word 0
cfg_dumpoff:	.word 0
cfg_buffer:		.word 0
cfg_offset:		.word 0
cfg_length:		.word 0
cfg_chunksiz:	.word 0
cfg_printf:		.word 0
cfg_read:		.word 0
cfg_patches:	.word 0, 0, 0, 0, 0, 0, 0, 0

main:
	addiu $sp, $sp, -0x1c
	sw $ra, 0x00($sp)
	sw $s7, 0x04($sp)
	sw $s4, 0x08($sp)
	sw $s3, 0x0c($sp)
	sw $s2, 0x10($sp)
	sw $s1, 0x14($sp)
	sw $s0, 0x18($sp)
	addiu $s0, $zero, cfg_buffer
	// branch to next instruction
	bal .next
	// delay slot: address mask
	lui $t4, 0xffff
.next:
	// store ra & 0xffff0000
	and $s7, $ra, $t4
	// buffer
	lw $s0, %lo(cfg_buffer)($s7)
	// offset
	lw $s1, cfg_offset($s7)
	// length
	lw $s2, 0x1c($s7)
	// bail out if length is zero
	beqz $s2, .out
	// delay slot: dump offset
	lw $s3, 0x10($s7)
	// branch to start_dump if we have a dump offset
	bnez $s3, .start_dump
	// delay slot: flash read function
	lw $s4, 0x28($s7)

	// patch code (affects only t4-t7)
	bal f_patch
	nop

	// if S4 is null, we're dumping RAM
	bnez $s4, .read_flash
	// delay slot: load flags
	lw $v0, 0x0c($s7)
	// use memory offset as buffer
	move $s0, $s1
	b .start_dump
	// delay slot: store new buffer
	sw $s0, 0x14($s7)

.read_flash:
	// set t4 to buffer
	move $t4, $s0
	// set t5 to length
	move $t5, $t6

.loop_bzero:
	// zero word at t4
	sw $zero, 0($t4)
	// loop until t5 == 0
	addiu $t5, $t5, -4
	bgtz $t5, .loop_bzero
	// delay slot: increment buffer
	addiu $t4, $t4, 4

	// set t4 if dump function is (buffer, offset, length)
	andi $t4, $v0, BCM2_READ_FUNC_OBL
	// set t5 if dump dunfction is (offset, buffer, length)
	andi $t5, $v0, BCM2_READ_FUNC_BOL
	// set a0 = &buffer, a1 = offset, a2 = length
	addiu $a0, $s7, 0x14
	move $a1, $s1
	move $a2, $s2
	// if t4: set a0 = buffer
	movn $a0, $s0, $t4
	// if t5: set a0 = offset and a1 = buffer
	movn $a0, $s1, $t5
	movn $a1, $s0, $t5
	// read from flash
	jalr $s4
	// leave this here!
	nop

.start_dump:
	// save s2 (remaining length)
	move $t6, $s2
	// set s2 to MIN(remaining length, chunk size)
	lw $s2, 0x20($s7)
	slt $t4, $t6, $s2
	movn $s2, $t6, $t4
	// increment buffer, offset and dump offset
	addu $s0, $s0, $s3
	addu $s1, $s1, $s3
	addu $s3, $s3, $s2
	// store dump offset
	sw $s3, 0x10($s7)
	// load remaining length, decrement by s2, and store
	lw $t4, 0x1c($s7)
	subu $t4, $t4, $s2
	sw $t4, 0x1c($s7)
	// set s4 to print function
	lw $s4, 0x24($s7)

.loop_line:
	// 4 words per line
	ori $s3, $zero, 4
	// load code offset
	move $a0, $s7

.loop_words:
	// printf(":%x", *s0)
	addiu $a0, $a0, 4
	jalr $s4
	lw $a1, 0($s0)
	// increment offset and buffer
	addiu $s0, $s0, 4
	addiu $s1, $s1, 4
	// decrement length and loop counter
	addi $s2, $s2, -4
	addi $s3, $s3, -1
	bgtz $s3, .loop_words
	// printf("\r\n")
	move $a0, $s7
	jalr $s4
	addiu $a0, $a0, 0x8
	// branch to loop_line if length > 0
	bgtz $s2, .loop_line
	// delay slot
	nop
.out:
	// restore code
	//bal f_patch
	//nop
	// restore registers
	lw $ra, 0x00($sp)
	lw $s7, 0x04($sp)
	lw $s4, 0x08($sp)
	lw $s3, 0x0c($sp)
	lw $s2, 0x10($sp)
	lw $s1, 0x14($sp)
	lw $s0, 0x18($sp)
	jr $ra
	addiu $sp, $sp, 0x1c

f_patch:
	// maximum of 4 words can be patched
	ori $v0, $zero, 4
	// pointer to first patch blob
	addiu $v1, $s7, 0x2c
.loop_patch:
	// load patch offset
	lw $a0, 0($v1)
	// break if patch offset is zero
	beqz $a0, .patch_done
	// delay slot: load patch word
	lw $t4, 4($v1)
	// load current word at offset
	lw $t5, 0($a0)
	// patch word at offset
	sw $t4, 0($a0)
	// store original word in patch (this way, calling this
	// function again will restore the original code)
	sw $t5, 4($v1)
	// decrement counter
	addiu $v0, $v0, -1
	// loop until we've reached the end
	bgtz $v0, .loop_patch
	// delay slot: set pointer to next patch blob
	addiu $v1, $v1, 8
.patch_done:
	jr $ra
	nop

checksum:
	.word 0





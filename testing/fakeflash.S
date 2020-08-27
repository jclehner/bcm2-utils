// Load at offset 0x80010000
// Compile with
// mips-linux-gnu-gcc -EB -c fakeflash.S -mips4 -mno-abicalls -ffreestanding -o fakeflash.o
// mips-linux-gnu-objcopy -j .text -O binary fakeflash.o fakeflash.bin

.set noreorder
.equ printf, 0x83f8b0c0
.text

.ascii "DBUG"

// 0x80001004
FakeFlashErase:
	addiu $sp, -4
	sw $ra, 0($sp)

	move $a2, $a1
	move $a1, $a0
	lui $a0, 0x8001
	li $v0, printf
	jalr $v0
	ori $a0, sErase

	lw $ra, 0($sp)
	jr $ra
	addiu $sp, 4

// 0x80001034
FakeFlashWrite:
	addiu $sp, -4
	sw $ra, 0($sp)

	move $a3, $a2
	move $a2, $a1
	move $a1, $a0
	lui $a0, 0x8001
	li $v0, printf
	jalr $v0
	ori $a0, sWrite

	lw $ra, 0($sp)
	jr $ra
	addiu $sp, 4

// 0x80010068
FakeFlashRead:
	addiu $sp, -4
	sw $ra, 0($sp)

	move $a3, $a2
	move $a2, $a1
	move $a1, $a0
	lui $a0, 0x8001
	li $v0, printf
	jalr $v0
	ori $a0, sRead

	lw $ra, 0($sp)
	jr $ra
	addiu $sp, 4

sErase:
	.asciz "FakeFlashErase: off=0x%08x, len=%d\r\n"
sWrite:
	.asciz "FakeFlashWrite: buf=0x%08x, off=0x%08x, len=%d\r\n"
sRead:
	.asciz "FakeFlashRead: off=0x%08x, buf=0x%08x, len=%d\r\n"

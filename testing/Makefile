MIPS=mips-linux-gnu-

.PHONY: upload clean fakeflash.bin

fakeflash.bin: fakeflash.S
	$(MIPS)gcc -EB -c fakeflash.S -mips4 -mno-abicalls -ffreestanding -o fakeflash.o
	$(MIPS)objcopy -j .text -O binary fakeflash.o fakeflash.bin
	$(MIPS)objdump -t -d --adjust-vma=0x80010000 fakeflash.o

clean:
	rm -f *.bin *.o

upload:
	../bcm2dump -svv write /dev/ttyUSB0 ram 0x80010000 fakeflash.bin


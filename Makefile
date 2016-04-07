CC ?= cc
CFLAGS ?= -Wall -g

bcm2cfg_OBJ = bcm2cfg.o nonvol.o
bcm2dump_OBJ = bcm2dump.o code.o bootloader.o \
			   mipsasm.o progress.o profile.o \
			   serial.o

.PHONY: all clean

all: bcm2cfg bcm2dump

bcm2cfg: $(bcm2cfg_OBJ) nonvol.h
	$(CC) $(CFLAGS) $(bcm2cfg_OBJ) -o bcm2cfg -lssl -lcrypto

bcm2dump: $(bcm2dump_OBJ) bcm2dump.h
	$(CC) $(CFLAGS) $(bcm2dump_OBJ) -o bcm2dump

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f bcm2cfg bcm2dump *.o

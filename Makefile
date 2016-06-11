CC = gcc
CXX = g++
CFLAGS ?= -Wall -g -DVERSION=\"$(shell git describe --always)\"
CXXFLAGS ?= $(CFLAGS) -std=c++11
PREFIX ?= /usr/local

bcm2cfg_OBJ = common.o nonvol.o profile.o bcm2cfg.o
bcm2dump_OBJ = common.o code.o bootloader.o \
			   mipsasm.o progress.o profile.o \
			   serial.o bcm2dump.o cm.o
iotest_OBJ = io.o dumper.o writer.o interface.o \
	util.o progress.o mipsasm.o iotest.o profile.o

.PHONY: all clean

all: bcm2cfg bcm2dump

bcm2cfg: $(bcm2cfg_OBJ) nonvol.h
	$(CXX) $(CXXFLAGS) $(bcm2cfg_OBJ) -o bcm2cfg -lssl -lcrypto

bcm2dump: $(bcm2dump_OBJ) bcm2dump.h
	$(CC) $(CFLAGS) $(bcm2dump_OBJ) -o bcm2dump

iotest: $(iotest_OBJ)
	$(CXX) $(CXXFLAGS) $(iotest_OBJ) -o iotest

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

%.o: %.cc
	$(CXX) -c $(CXXFLAGS) $< -o $@

clean:
	rm -f bcm2cfg bcm2dump *.o

install: all
	install -m 755 bcm2cfg $(PREFIX)/bin
	install -m 755 bcm2dump $(PREFIX)/bin

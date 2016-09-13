CC = gcc
CXX = g++
CFLAGS ?= -Wall -g -DVERSION=\"$(shell git describe --always)\"
CXXFLAGS ?= $(CFLAGS) -std=c++14 -Wnon-virtual-dtor
PREFIX ?= /usr/local

bcm2cfg_OBJ = nonvol.o profile.o bcm2cfg.o profiledef.o
bcm2dump_OBJ = io.o rwx.o interface.o ps.o bcm2dump.o \
	util.o progress.o mipsasm.o profile.o profiledef.o
nonvoltest_OBJ = util.o nonvol2.o nonvoltest.o nonvoldef.o gwsettings.o profile.o profiledef.o

t_nonvol_OBJ = util.o nonvol2.o t_nonvol.o

.PHONY: all clean

all: bcm2dump bcm2cfg nonvoltest t_nonvol

bcm2cfg: $(bcm2cfg_OBJ) nonvol.h
	$(CXX) $(CXXFLAGS) $(bcm2cfg_OBJ) -o $@ -lssl -lcrypto

bcm2dump: $(bcm2dump_OBJ) bcm2dump.h
	$(CXX) $(CXXFLAGS) $(bcm2dump_OBJ) -o $@

nonvoltest: $(nonvoltest_OBJ)
	$(CXX) $(CXXFLAGS) $(nonvoltest_OBJ) -o $@ -lssl -lcrypto

t_nonvol: $(t_nonvol_OBJ)
	$(CXX) $(CXXFLAGS) $(t_nonvol_OBJ) -o $@

%.o: %.c %.h
	$(CC) -c $(CFLAGS) $< -o $@

%.o: %.cc %.h
	$(CXX) -c $(CXXFLAGS) $< -o $@

check: t_nonvol
	./t_nonvol

clean:
	rm -f bcm2cfg bcm2dump nonvoltest t_nonvol *.o

install: all
	install -m 755 bcm2cfg $(PREFIX)/bin
	install -m 755 bcm2dump $(PREFIX)/bin

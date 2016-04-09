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
 * along with nmrpflash.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef BCM2_UTILS_H
#define BCM2_UTILS_H
#include <stdbool.h>
#include <stdint.h>

#define BCM2_PATCH_NUM 4

#ifdef __cplusplus
extern "C" {
#endif

enum bcm2_read_func_mode
{
	// pointer to buffer, offset length
	BCM2_READ_FUNC_PBOL = 0,
	// buffer, offset, length
	BCM2_READ_FUNC_BOL = 1 << 0,
	// offset, buffer, length
	BCM2_READ_FUNC_OBL = 1 << 1,
};

enum bcm2_func_ret
{
	// ignore return value
	BCM2_RET_VOID = 0,
	// returns zero on success
	BCM2_RET_OK_0 = 1 << 0,
	// returns zero on error
	BCM2_RET_ERR_0 = 1 << 1,
	// returns length on success
	BCM2_RET_OK_LEN = 1 << 2
};

struct bcm2_partition {
	// partition name
	char name[32];
	// offset (absolute, not relative
	// to address space begin)
	uint32_t offset;
	// size
	uint32_t size;
};

struct bcm2_func {
	// address of this function
	uint32_t addr;
	// mode of this function. interpretation
	// depends on actual function.
	uint32_t mode;
	// return value type
	enum bcm2_func_ret retv;
	// patches to be applied before using 
	// this function.
	struct {
		// patch this address...
		uint32_t addr;
		// ... with this word
		uint32_t word;
	} patch[BCM2_PATCH_NUM];
};

struct bcm2_addrspace {
	// short name to identify the address space. you
	// should at least specify one address space called
	// "ram".
	char name[16];
	// set to true if this is a memory-mapped address space
	// (automatically set for address spaces named "ram").
	bool ram;
	// minimum offset of this address space
	uint32_t min;
	// size of this address space. can be 0 to disable size
	// check
	uint32_t size;
	// partitions within this address space
	struct bcm2_partition parts[16];
	// read function to read from this address space (can
	// be left blank for ram segment)
	struct bcm2_func read;
	// not yet used
	struct bcm2_func write;
};

struct bcm2_profile {
	// short name that is used to select a profile
	char name[16];
	// pretty device name
	char pretty[64];
	// little endian MIPS (not supported at the moment)
	bool mipsel;
	// signature for ProgramStore images
	uint16_t pssig;
	// baudrate of the bootloader console
	uint32_t baudrate;
	// location in memory where we can store read images
	uint32_t buffer;
	// length of buffer (if 0, buffer will be checked 
	// against "ram" address space)
	uint32_t buflen;
	// address mask for uncached mips segment
	uint32_t kseg1mask;
	// address where dump code can be loaded (dump code
	// is short, currently around 512 bytes)
	uint32_t loadaddr;
	// memory address of a bootloader function that behaves
	// like printf (a0 = format string, a1...aX format args)
	uint32_t printf;
	// not used
	uint32_t scanf;
	// a location in memory with a constant value (ideally a
	// bootloader string), which can be used to automatically
	// identify the connected device
	struct {
		uint32_t addr;
		char data[32];
	} magic;
	// a key that is appended to the configuration file data
	// before calculating its checksum. specify as a hex string 
	char cfg_md5key[65];
	// default encryption keys for backups without a password
	char cfg_defkeys[8][64];
	// key derivation function for encrypted configuration files.
	// key is a 32 byte buffer (256 bit RSA). return false if
	// key derivation failed.
	bool (*cfg_keyfun)(const char *password, unsigned char *key);
	// address spaces that can be dumped
	struct bcm2_addrspace spaces[4];
};

extern struct bcm2_profile bcm2_profiles[];

struct bcm2_profile *bcm2_profile_find(const char *name);
struct bcm2_addrspace *bcm2_profile_find_addrspace(
		struct bcm2_profile *profile, const char *name);
struct bcm2_partition *bcm2_addrspace_find_partition(
		struct bcm2_addrspace *addrspace, const char *name);

#ifdef __cplusplus
}
#endif

#endif

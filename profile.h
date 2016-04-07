#ifndef BCM2_UTILS_H
#define BCM2_UTILS_H
#include <stdbool.h>
#include <stdint.h>
#define BCM2_PATCH_NUM 4

enum bcm2_read_func_mode
{
	// pointer to buffer, offset length
	BCM2_READ_FUNC_PBOL = 0,
	// buffer, offset, length
	BCM2_READ_FUNC_BOL = 1 << 0,
	// offset, buffer, length
	BCM2_READ_FUNC_OBL = 1 << 1,
};

struct bcm2_partition {
	char name[32];
	uint32_t offset;
	uint32_t size;
};

struct bcm2_func {
	uint32_t addr;
	uint32_t mode;
	struct {
		uint32_t addr;
		uint32_t word;
	} patch[BCM2_PATCH_NUM];
};

struct bcm2_addrspace {
	char name[32];
	bool ram;
	uint32_t min;
	uint32_t size;
	struct bcm2_partition parts[16];
	struct bcm2_func read;
	// not yet used
	struct bcm2_func write;
};

struct bcm2_profile {
	char name[32];
	char pretty[64];
	uint16_t pssig;
	uint32_t baudrate;
	uint32_t buffer;
	uint32_t buflen;
	uint32_t kseg1mask;
	uint32_t loadaddr;
	uint32_t printf;
	// not yet used
	uint32_t scanf;
	struct {
		uint32_t addr;
		char data[32];
	} magic;

	struct bcm2_addrspace spaces[4];
};

extern struct bcm2_profile bcm2_profiles[];

struct bcm2_profile *bcm2_profile_find(const char *name);
struct bcm2_addrspace *bcm2_profile_find_addrspace(
		struct bcm2_profile *profile, const char *name);
struct bcm2_partition *bcm2_addrspace_find_partition(
		struct bcm2_addrspace *addrspace, const char *name);

#endif

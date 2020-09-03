#ifndef BCM2_DUMP_RWCODE2_H
#define BCM2_DUMP_RWCODE2_H
#include "profile.h"

#ifdef __cplusplus
extern "C" {
#endif

// Environment for uploading and executing code
// on another machine. Memory layout:
//
// 0x0000      entry code
// 0x0100      entry data
//
// entry data:
//   u32       flags
//   u32       retval
//   u32       function
//   u32       argc
//   u32[]     argv
//
// entry code does:
// * locate itself in RAM
// * load private data
//   * if function call, do so now


struct bcm2_read_args
{
	char str_x[4];
	char str_nl[4];
	uint32_t flags;
	uint32_t buffer;
	uint32_t offset;
	uint32_t length;
	uint32_t chunklen;
	uint32_t index;
	uint32_t printf;
	uint32_t fl_read;
	struct bcm2_patch patches[BCM2_PATCH_NUM];
} __attribute__((aligned(4)));

void mips_read();

struct bcm2_write_args
{
	char str_2x[8];
	char str_nl[4];
	uint32_t flags;
	uint32_t buffer;
	uint32_t offset;
	uint32_t length;
	uint32_t chunklen;
	uint32_t index;
	uint32_t printf;
	uint32_t xscanf;
	uint32_t getline;
	uint32_t fl_erase;
	uint32_t fl_write;
	struct bcm2_patch erase_patches[BCM2_PATCH_NUM];
	struct bcm2_patch write_patches[BCM2_PATCH_NUM];
} __attribute__((aligned(4)));

void mips_write();

#ifdef __cplusplus
}
#endif
#endif

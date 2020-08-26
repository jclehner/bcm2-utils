#include "rwcode2.h"
#include "profile.h"

#define CONCAT(a, b) CONCAT2(a, b)
#define CONCAT2(a, b) CONCAT3(a, b)
#define CONCAT3(a, b) a ## b

#ifdef __mips__
#define RWCODE_INIT_ARGS(name) \
	asm volatile ( \
		"  lui %0, 0xffff\n" \
		"  bal L_next\n" \
		"L_next:\n" \
		"  and %0, $ra, %0\n" \
		/*"  addiu %0, -%1\n" \*/ \
		: \
		"=r" (args) \
		: \
		/*"i" (sizeof(*args))*/ \
		: "$t4");
#else
#define RWCODE_INIT_ARGS(name) \
	name = &rwcode_args
#endif

#define RWCODE_BZERO(addr, len) \
	do { \
		uint32_t i; \
		for (i = 0; i < len; i += 4) { \
			*(uint32_t*)(addr + i) = 0; \
		} \
	} while (0)

#define RWCODE_PATCH(patches) \
	do { \
		uint32_t i; \
		for (i = 0; i < sizeof(patches)/sizeof(patches[0]); ++i) { \
			uint32_t* p= (uint32_t*)patches[i].addr; \
			if (!p) { \
				break; \
			} \
			uint32_t w = *p; \
			*p = patches[i].word; \
			patches[i].word = w; \
		} \
	} while (0)

#define MIN(a, b) ((a) < (b) ? (a) : (b))

typedef uint32_t (*fl_read_fun)(uint32_t, uint32_t, uint32_t);
typedef int (*printf_fun)(const char*, ...);

// These functions will be executed on the modem itself, NOT
// the device running bcm2dump. As such, certain restrictions
// apply:
//
// * No global data.
// * No direct function calls (only function pointers).
// * Data shared between host and target must be fixed-width

void mips_read()
{
	struct bcm2_read_args* args;
	RWCODE_INIT_ARGS(args);

	if (!args->length) {
		return;
	}

	// TODO allow chunked reads

	if (args->fl_read) {
		//RWCODE_BZERO(args->buffer, args->length);

		uint32_t arg1, arg2;

		if (args->flags & BCM2_READ_FUNC_OBL) {
			arg1 = args->offset;
			arg2 = args->buffer;
		} else {
			arg2 = args->offset;

			if (args->flags & BCM2_READ_FUNC_PBOL) {
				arg1 = (uint32_t)&args->buffer;
			} else {
				arg1 = args->buffer;
			}
		}

		RWCODE_PATCH(args->patches);
		uint32_t ret = ((fl_read_fun)args->fl_read)(arg1, arg2, args->length);
		RWCODE_PATCH(args->patches);

		args->fl_read = 0;
		args->offset = 0;
	}

	uint32_t* buffer = (uint32_t*)(args->buffer + args->offset);
	uint32_t len = MIN(args->length, args->chunklen);
	args->length -= len;
	args->offset += len;

	do {
		for (int i = 0; i < 4; ++i) {
			((printf_fun)args->printf)(args->str_x, *buffer++);
		}
		((printf_fun)args->printf)(args->str_nl);
	} while ((len -= 16));
}

void mips_write()
{


}

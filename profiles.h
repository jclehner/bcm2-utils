#ifndef BCM2UTILS_PROFILES_H
#define BCM2UTILS_PROFILES_H
#include <stdbool.h>
#include <stdint.h>

struct bcm2_profile {
	char name[32];
	bool has_md5key;
	uint8_t md5key[16];
};

#endif

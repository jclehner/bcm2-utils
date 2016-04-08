#ifndef BCM2UTILS_COMMON_H
#define BCM2UTILS_COMMON_H
#include <stdbool.h>
#include "profile.h"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

bool strtou(const char *str, unsigned *n, bool quiet);
bool handle_common_opt(int opt, char *arg, int *verbosity,
		struct bcm2_profile **profile);

#endif

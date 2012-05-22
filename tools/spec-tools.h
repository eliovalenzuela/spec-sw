
#include "loader-userspace.h"

void *spec_map_area(int bus, int dev, int bar, size_t size);

enum {
	BASE_BAR0 = 0,	/* for wrpc etc (but lm32 is at 0x80000 offset) */
	BASE_BAR2 = 2,
	BASE_BAR4 = 4	/* for gennum-internal registers */
};           



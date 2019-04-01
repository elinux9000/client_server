#include "common.h"
unsigned long elapsed_time(void)
{
	struct timespec tp;
	static unsigned long start;
	unsigned long now;

	clock_gettime(CLOCK_REALTIME, &tp);
	if (start == 0) {
		start = tp.tv_sec * 1e9;
		start += tp.tv_nsec;
	}
	now = tp.tv_sec * 1e9;
	now += tp.tv_nsec;

	return (now - start)/1000000;
}

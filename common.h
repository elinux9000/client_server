#ifndef __COMMON_H__
#define __COMMON_H__
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <sys/signal.h>
#include <printf.h>
#include <getopt.h>
#include <stdio.h>
#include <sys/signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <linux/unistd.h>
#include <netinet/tcp.h>
#include "steque.h"

unsigned long elapsed_time(void);

#define PRINT_THREAD_ID true
#define PRINT_ELAPSED_TIME true
#define get_tid() (syscall(__NR_gettid))
#define dbg_printf(...) do {\
	if (debug_output) {\
		flockfile(stdout);\
		if (PRINT_ELAPSED_TIME) {\
			unsigned long t = elapsed_time();\
			printf("%01lu.%03lu:", t / 1000, t % 1000);\
		} \
		if (PRINT_THREAD_ID)\
			printf("pid %lu:", syscall(__NR_gettid));\
		printf(__VA_ARGS__);\
		funlockfile(stdout);\
	} \
} \
while (0)
#define dbg_perror()	do { printf("In %s at line %d ", __func__, __LINE__); perror(""); } while (0)

#define HANDLE_FATAL_ERROR do { printf("fatal error in %s line %d\n", __func__, __LINE__); exit(1); }  while (0)

#define DEFAULT_PORT 54321
#endif // __COMMON_H__

#include "common.h"


#define USAGE                                                                         \
"usage:	client [options]\n"                                                              \
"options:\n"                                                                          \
"  -p [listen_port]    Listen port (Default is 50419)\n"                              \
"  -t [thread_count]   Num worker threads (Default is 7, Range is 1-512)\n"           \
"  -s [server]         The server to connect to\n"         \
"  -h                  Show this help message\n"

//File scope variables
bool debug_output;// = true;

//Global variables
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_wait = PTHREAD_COND_INITIALIZER;

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option g_long_options[] = {
	{"port",          required_argument,      NULL,           'p'},
	{"thread-count",  required_argument,      NULL,           't'},
	{"segment-count", required_argument,      NULL,           'n'},
	{"segment-size",  required_argument,      NULL,           'z'},
	{"server",        required_argument,      NULL,           's'},
	{"help",          no_argument,            NULL,           'h'},
	{NULL,            0,                      NULL,            0}
};


static void _sig_handler(int signo)
{
	if (signo == SIGTERM || signo == SIGINT) {
		//TODO wake all threads

		exit(signo);
	}
}
/* Main ========================================================= */
int main(int argc, char **argv)
{
	int option_char = 0;
	unsigned short port = DEFAULT_PORT;
	unsigned short nworkerthreads = 1;
	const char *server = "localhost";

	/* disable buffering on stdout so it prints immediately */
	setbuf(stdout, NULL);
	if (signal(SIGINT, _sig_handler) == SIG_ERR) {
		fprintf(stderr, "Can't catch SIGINT...exiting.\n");
		exit(EXIT_FAILURE);
	}

	if (signal(SIGTERM, _sig_handler) == SIG_ERR) {
		fprintf(stderr, "Can't catch SIGTERM...exiting.\n");
		exit(EXIT_FAILURE);
	}

	/* Parse and set command line arguments */
	while ((option_char = getopt_long(
		argc, argv, "x:n:ls:p:t:z:h", g_long_options, NULL)) != -1) {
		switch (option_char) {
		default:
			fprintf(stderr, "%s", USAGE);
			exit(__LINE__);
		case 'p': // listen-port
			port = atoi(optarg);
			break;
		case 't': // thread-count
			nworkerthreads = atoi(optarg);
			break;
		case 'x':
			debug_output = (atoi(optarg) == 1);
			break;
		case 'h': // help
			fprintf(stdout, "%s", USAGE);
			exit(0);
			break;
		}
	}

	if (!server) {
		fprintf(stderr, "Invalid (null) server name\n");
		exit(__LINE__);
	}

	if (port < 1024) {
		fprintf(stderr, "Invalid port number\n");
		exit(__LINE__);
	}

	if ((nworkerthreads < 1) || (nworkerthreads > 512)) {
		fprintf(stderr, "Invalid number of worker threads\n");
		exit(__LINE__);
	}

	printf("client options:threads=%d server=%s port=%d\n", nworkerthreads, server, port);

}

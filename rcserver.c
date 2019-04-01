#include "common.h"
#include "network.h"
#include "rcserver.h"

#define MAX_CACHE_REQUEST_LEN 5041
#define SIZE_OF_SOCKET_ARRAY 100
//file scope variables
static int nthreads = 3;
bool debug_output;// = true;
static pthread_mutex_t mutex_index;
static pthread_cond_t boss;
static pthread_cond_t worker;
static pthread_t *threads;
static steque_t *steque;
char *ptr = (char *)1;

unsigned short g_port = DEFAULT_PORT;
bool start_threads;	//don't start threads until this is true

//Prototypes
void *worker_func(void *arg);
void *boss_func(void *portno);

#define USAGE \
"usage:	server [options]\n"\
"	options:\n"\
"	-p [port]		port number\n"\
"	-t [thread_count]	thread count for work queue\n"\
"	-d			enable debug output\n"\
"	-h			show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option g_long_options[] = {
	{"nthreads",	required_argument,      NULL,           't'},
	{"dbg_output",	required_argument,      NULL,           'x'},
	{"port",	required_argument,      NULL,           'p'},
	{"help",	no_argument,            NULL,           'h'},
	{NULL,          0,                      NULL,             0}
};

void usage(void)
{
	fprintf(stdout, "%s", USAGE);
}
const char *__asan_default_options()
{
	return "verbosity=1:debug=1;check_initialization_order=1";
}

void thread_cleanup(void *arg)
{
	if (pthread_mutex_unlock(&mutex_index) == 0)
		dbg_printf("unlocked mutex\n");
	else
		dbg_printf("unlocked a mutex it didn't own\n");
}
int initialize_primitives(void)
{
	pthread_mutexattr_t attr;

	if (pthread_mutexattr_init(&attr) != 0) {
		perror("mutex attr");
		exit(EXIT_FAILURE);
	}
	if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK) != 0) {
		perror("mutex error check");
		exit(EXIT_FAILURE);
	}
	pthread_mutex_init(&mutex_index, &attr);
	pthread_mutexattr_destroy(&attr);

	pthread_cond_init(&boss, NULL);
	pthread_cond_init(&worker, NULL);

	return EXIT_SUCCESS;

}
void create_steque(void)
{
	steque = calloc(sizeof(*steque), 1);
	if (!steque) {
		HANDLE_FATAL_ERROR;
	}
	steque_init(steque);
}
int create_thread_pool(unsigned short nthreads)
{
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	dbg_printf("creating %u threads\n", nthreads);
	threads = calloc(sizeof(*threads), nthreads+1);
	if (threads) {
		for (unsigned short i = 0; i < nthreads; i++) {
			//create thread
			if (i == 0)
				pthread_create(&threads[i], &attr, boss_func, NULL);
			else
				pthread_create(&threads[i], &attr, worker_func, NULL);
		}
	}
	pthread_attr_destroy(&attr);
	if (threads)
		return EXIT_SUCCESS;
	return EXIT_FAILURE;
}
void wait_for_green(char *msg)
{
	dbg_printf("%s waiting to run\n", msg);
	while (!start_threads)
		usleep(10*1000);
	dbg_printf("%s running\n", msg);

}
void *boss_func(void *arg)
{
	//Loops forever
	int listen_sock_fd;
	int max_pending = 10;
	int sock;
	int *sock_ptr = NULL;

	wait_for_green("boss");
	listen_sock_fd = open_listening_socket("localhost", g_port, max_pending);
	if (listen_sock_fd == -1) {
		dbg_printf("could not open socket\n");
		perror("socket error");
		raise(SIGTERM);
		return NULL;
	}
	while (true) { //serve files then exit

		sock = wait_for_connection(listen_sock_fd);
		if (sock == -1) {
			dbg_printf("Error accepting connection\n");
			sleep(1);
		}
		else {
			pthread_mutex_lock(&mutex_index);
			while (steque_size(steque) >= SIZE_OF_SOCKET_ARRAY)
				pthread_cond_wait(&boss, &mutex_index);
			sock_ptr = malloc(sizeof(*sock_ptr));
			if (!sock_ptr)
				HANDLE_FATAL_ERROR;
			*sock_ptr = sock;
			steque_enqueue(steque, sock_ptr);
			pthread_cond_broadcast(&worker);
			pthread_mutex_unlock(&mutex_index);
		}
	}
}
static void handle_error(const char *msg, unsigned int line)
{
	dbg_printf("Fatal error in %s at line %u\n", msg, line);
	exit(-1);
}
int setup_parameter(int sock, char *parm)
{
	int status;
	char buf[1024];

	//dbg_printf("Waiting for %s\n", parm);
	status = read_from_socket(sock, buf, sizeof(buf));
	if (status != 0) {
		handle_error(__func__, __LINE__);
		return -1;
	}
	if (strncmp(buf, parm, strlen(parm)) == 0)
		return 0;
	dbg_printf("Expected %s got %s\n", parm, buf);
	return -1;

}
int read_parameter(int sock, char *parm, char *buf)
{
	//returns 0 on success
	char s[1024];
	int status;

	status = read_from_socket(sock, buf, sizeof(buf));
	if (status != 0) {
		handle_error(__func__, __LINE__);
		return -1;
	}

	sprintf(s, "%s\n", parm);
	status = send_to_socket(sock, s);
	if (status != 0) {
		handle_error(__func__, __LINE__);
		return -1;
	}
	if (strncmp(buf, parm, strlen(parm) != 0)) {
		return -1;
	}
	return 0;
}
void enable_signals(void)
{
	sigset_t set;
	int s;

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	s = pthread_sigmask(SIG_UNBLOCK, &set, NULL);
	if (s != 0) {
		dbg_printf("Could not set sigmask\n");
		perror("mask");
		exit(1);
	}
}
void block_signals(void)
{
	sigset_t set;
	int s;

	sigfillset(&set);
	s = pthread_sigmask(SIG_BLOCK, &set, NULL);
	if (s != 0) {
		dbg_printf("Could not set sigmask\n");
		perror("mask");
		exit(1);
	}
	if (pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL) != 0) {
		perror("cancel type");
		exit(1);
	}
}
void *worker_func(void *arg)
{
	// Thread function
	// Wait to dequeue a request then serve
	int *sock = NULL;
	char buf[1024];

	pthread_cleanup_push(thread_cleanup, NULL);
	wait_for_green("worker");

	while (true) {

		dbg_printf("waiting on mutex\n");
		pthread_mutex_lock(&mutex_index);
		while (steque_size(steque) == 0) {
			pthread_cond_wait(&worker, &mutex_index);
		}
		//dequeue
		sock = steque_pop(steque);
		//unlock
		pthread_cond_signal(&boss);
		pthread_mutex_unlock(&mutex_index);

		if (read_from_socket(*sock, buf, sizeof(buf)) > 0) {
			dbg_printf("recv:%s", buf);
		}
		else
			dbg_printf("error");

		if (sock) {
			close(*sock);
			free(sock);
			sock = NULL;
		}
	}
	dbg_printf("Thread exiting\n");
	pthread_cleanup_pop(NULL);

	return NULL;
}
void print_memory(char *addr, int n)
{
	for (int i = 0; i < n/8; i++) {
		for (int j = 0; j < 8; j++) {
			printf("0x%02X ", (unsigned char)*addr);
			addr++;
		}
		printf("\n");

	}
}
static void sig_handler(int signo)
{
	dbg_printf("terminating\n");
	if (signo == SIGTERM || signo == SIGINT) {

	//	pthread_mutex_unlock(&mutex_index);
	//	pthread_cond_broadcast(&worker);
	//	for (int i = 0; i < nthreads; i++) {
	//		dbg_printf("Cancelling thread %d\n", i);
	//		pthread_cancel(threads[i]);

	//		pthread_join(threads[i], NULL);
	//		dbg_printf("thread %d exited\n", i);
	//	}
	//	if (steque) {
	//		steque_destroy(steque);
	//		free(steque);
	//	}
	//	if (threads)
	//		free(threads);
	//	exit(signo);
	}
}
int main(int argc, char **argv)
{
	char option_char;
	struct sigaction new_action, old_action;

	/* disable buffering to stdout */
	setbuf(stdout, NULL);

	while ((option_char = getopt_long(argc, argv, "dh:t:p:", g_long_options, NULL)) != -1) {
		switch (option_char) {
		case 'h': // help
			usage();
			exit(0);
			break;
		case 't': // thread-count
			nthreads = atoi(optarg);
			break;
		case 'd':
			debug_output = true;
			break;
		case 'p':
			g_port = atoi(optarg);
			break;
		default:
			usage();
			exit(1);
		}
	}

	if ((nthreads > 31415) || (nthreads < 2)) {
		fprintf(stderr, "Invalid number of threads\n");
		exit(EXIT_FAILURE);
	}

	dbg_printf("server options:threads=%d port=%d debug_output=%d\n", nthreads, g_port, debug_output);
	block_signals();
	create_steque();
	//Initialize synchronization primitives
	initialize_primitives();

	if (create_thread_pool(nthreads) != 0)
		handle_error(__func__, __LINE__);

	enable_signals();

	/* Set up the structure to specify the new action. */
	new_action.sa_handler = sig_handler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;

	sigaction(SIGINT, NULL, &old_action);
	//if (old_action.sa_handler != SIG_IGN)
	sigaction(SIGINT, &new_action, NULL);
	sigaction(SIGTERM, NULL, &old_action);
	//if (old_action.sa_handler != SIG_IGN)
	sigaction(SIGTERM, &new_action, NULL);

	sigset_t set;
	int s;
	int sig;

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	dbg_printf("mainline running\n");
	start_threads = true;

	for (;;) {
		s = sigwait(&set, &sig);
		dbg_printf("Signal handling thread got signal %d\n", sig);
		if (s == 0)
			break;
	}
	for (int i = 0; i < nthreads; i++) {
		dbg_printf("Cancelling thread %d\n", i);
		pthread_cancel(threads[i]);

		pthread_join(threads[i], NULL);
		dbg_printf("thread %d exited\n", i);
	}
	dbg_printf("the end\n");


	// Won't execute
	return 0;
}

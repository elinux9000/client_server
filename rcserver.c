#include "common.h"
#include "rcserver.h"

#define MAX_CACHE_REQUEST_LEN 5041
#define SIZE_OF_SOCKET_ARRAY 100
//file scope variables
static int nthreads = 3;
static bool debug_output;// = true;
static pthread_mutex_t mutex_index;
static pthread_cond_t boss;
static pthread_cond_t worker;
static pthread_t *threads;
static steque_t *steque;
char *ptr = (char *)1;

unsigned short g_port = 12345;
bool running = true;

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

void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET)
		return &(((struct sockaddr_in *)sa)->sin_addr);
	return &(((struct sockaddr_in6 *)sa)->sin6_addr);
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
static int send_to_socket(int sock_fd, char *buf)
{
	size_t total = 0;
	size_t len = strlen(buf);
	ssize_t bytes_written;

	while (total < len) {
		bytes_written = send(sock_fd, &buf[total], len - total, 0);
		if (bytes_written < 0)
			return -1;
		total += bytes_written;
	}
	return 0;
}
int open_listening_socket(char *host, unsigned short portno, unsigned int max_pending)
{
	//returns -1 on failure, socket on success
	struct addrinfo hints, *servinfo, *p;
	int yes = 1;
	int rv;
	char port_string[7];
	int sock_fd;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	sprintf(port_string, "%u", portno);

	rv = getaddrinfo(NULL, port_string, &hints, &servinfo);
	if (rv != 0) {
		dbg_printf("getaddrinfo: %s\n", gai_strerror(rv));
		return -1;
	}

	// loop through all the results and bind to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next) {
		sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (sock_fd == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
			perror("setsockopt");
			return -1;
		}
		// lose the pesky "address already in use" error message
		if (bind(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sock_fd);
			perror("server: bind");
			continue;
		}
		break;
	}
	freeaddrinfo(servinfo); // all done with this structure

	if (p == NULL)  {
		dbg_printf("server: failed to bind\n");
		return -1;
	}
	if (listen(sock_fd, max_pending) == -1) {
		perror("listen");
		return -1;
	}
	dbg_printf("Listening at port %u on %s\n", portno, host);
	return sock_fd;
}
int wait_for_connection(int sock_fd)
{
	//returns -1 on failure, socket on success
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	char s[INET6_ADDRSTRLEN];
	int new_fd;
	int result;
	int flag = 1;

	dbg_printf("Server waiting...\n");
	sin_size = sizeof(their_addr);
	new_fd = accept(sock_fd, (struct sockaddr *)&their_addr, &sin_size);
	if (new_fd == -1) {
		perror("accept");
		return -1;
	}
	inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof(s));
	dbg_printf("Connection from %s\n", s);

	result = setsockopt(new_fd,	/* socket affected */
	IPPROTO_TCP,            /* set option at TCP level */
	TCP_NODELAY,            /* name of option */
	(char *) &flag,         /* the cast is historical cruft */
	sizeof(int));           /* length of option value */
	if (result < 0)
		dbg_perror();
	return new_fd;
}
void *boss_func(void *arg)
{
	//Loops forever
	int listen_sock_fd;
	int max_pending = 10;
	int sock;
	int *sock_ptr = NULL;

	dbg_printf("boss thread\n");
	listen_sock_fd = open_listening_socket("localhost", g_port, max_pending);
	if (listen_sock_fd == -1) {
		printf("could not open socket\n");
		perror("socket error");
		exit(EXIT_FAILURE);
	}
	while (true) { //serve files then exit

		sock = wait_for_connection(listen_sock_fd);
		if (sock == -1) {
			dbg_printf("Error accepting connection\n");
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
int read_from_socket(int sock, char *buf)
{
	//returns 0 on success
	int bytes_received = 0;
	int status = 0;

	do {
		status = recv(sock, &buf[bytes_received], 1024-bytes_received, 0);
		if (status <= 0)
			return -1;
		bytes_received += status;
		if (buf[bytes_received-1] == '\n') {
			buf[bytes_received-1] = 0;//remove terminator
			break;
		}
	} while (bytes_received < 1024);//for safety
	return (bytes_received >= 1024);
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
	status = read_from_socket(sock, buf);
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

	status = read_from_socket(sock, buf);
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

	//sigemptyset(&set);
//sigaddset(&set, SIGINT);
//sigaddset(&set, SIGTERM);
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
	int *sock;

	pthread_cleanup_push(thread_cleanup, NULL);

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

		free(sock);
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
		running = false;

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

	dbg_printf("server options:threads=%d debug_output=%d\n", nthreads, debug_output);
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
	dbg_printf("running\n");
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

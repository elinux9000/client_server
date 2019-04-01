#include "common.h"
extern bool debug_output;

//Do not use any globals or statics in this file to keep it thread safe
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET)
		return &(((struct sockaddr_in *)sa)->sin_addr);
	return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}
int send_to_socket(int sock_fd, char *buf)
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

	dbg_printf("listening ...\n");
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
int read_from_socket(int sock, char *buf, size_t buf_len)
{
	//returns 0 on success
	//reads buf_len or until last byte received is carriage return.
	int bytes_received = 0;
	int status = 0;

	memset(buf, 0, buf_len);

	do {
		status = recv(sock, &buf[bytes_received], buf_len-bytes_received, 0);
		if (status <= 0)
			return -1;
		bytes_received += status;
		if (buf[bytes_received-1] == '\n') {
			break;
		}
	} while (bytes_received < buf_len);
	return bytes_received;
}

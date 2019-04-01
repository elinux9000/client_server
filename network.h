#ifndef __NETWORK_H__
#define __NETWORK_H__
void *get_in_addr(struct sockaddr *sa);
int send_to_socket(int sock_fd, char *buf);
int open_listening_socket(char *host, unsigned short portno, unsigned int max_pending);
int wait_for_connection(int sock_fd);
int read_from_socket(int sock, char *buf, size_t buf_len);
#endif

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>

int            initialize_socket(void);
int            accept_clients(int domain_sock, int server_sock, struct sockaddr_in client_addr, socklen_t client_addrlen);
void           send_fd(int domain_socket, int fd);
int            recv_fd(int socket);
struct pollfd *initialize_pollfds(int sockfd, int **client_sockets);
void           handle_new_connection(int sockfd, int **client_sockets, nfds_t *max_clients, struct pollfd **fds);
void           socket_close(int sockfd);

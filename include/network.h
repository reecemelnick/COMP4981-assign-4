#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>

int            initialize_socket(void);
int            accept_clients(int domain_sock, int server_sock, struct sockaddr_in client_addr, socklen_t client_addrlen);
void           send_fd(int domain_socket, int fd);
int            recv_fd(int socket, int *og_fd);
struct pollfd *initialize_pollfds(int sockfd, int **client_sockets);
void           handle_new_connection(int sockfd, int **client_sockets, nfds_t *max_clients, struct pollfd **fds);
void           socket_close(int sockfd);
void           set_socket_nonblock(int sockfd);
void           handle_new_socket(void);
void           handle_client_data(struct pollfd *fds, const int *client_sockets, const nfds_t *max_clients, int domain_sock);
void           handle_client_disconnection(int **client_sockets, nfds_t *max_clients, struct pollfd **fds, nfds_t client_index);
void           set_fd_blocking(int fd);
void           read_original_fd(int domain_socket, int **client_sockets, struct pollfd **fds, nfds_t *max_clients);

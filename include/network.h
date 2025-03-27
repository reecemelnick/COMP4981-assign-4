#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>

int            initialize_socket(void);
void           send_fd(int domain_socket, int fd);
int            recv_fd(int socket, int *og_fd);
struct pollfd *initialize_pollfds(int sockfd, int **client_sockets);
void           handle_new_connection(int sockfd, int **client_sockets, nfds_t *max_clients, struct pollfd **fds);
void           socket_close(int sockfd);
void           set_socket_nonblock(int sockfd);
void           handle_new_socket(void);
void           handle_client_data(struct pollfd *fds, int *client_sockets, nfds_t *max_clients, int domain_sock);
void           handle_client_disconnection(int **client_sockets, nfds_t *max_clients, struct pollfd **fds, nfds_t client_index);
int            worker_handle_client(int client_sock);
int            create_domain_socket(void);
int            connect_to_domain(void);
void           set_fd_blocking(int fd);
void           read_original_fd(int domain_socket, int **client_sockets, struct pollfd **fds, nfds_t *max_clients);

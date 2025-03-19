#include <arpa/inet.h>
#include <netinet/in.h>

int  initialize_socket(void);
int  accept_clients(int domain_sock, int server_sock, struct sockaddr_in client_addr, socklen_t client_addrlen);
void send_fd(int domain_socket, int fd);
int  recv_fd(int socket);

#include <arpa/inet.h>
#include <netinet/in.h>

int initialize_socket(void);
int accept_clients(int server_sock, struct sockaddr_in host_addr, socklen_t host_addrlen);
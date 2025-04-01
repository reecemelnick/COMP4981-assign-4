#include "../include/network.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define PORT 8000

int initialize_socket(void)
{
    struct sockaddr_in host_addr;
    socklen_t          host_addrlen;

    // create ipv4 socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);    // NOLINT
    if(sockfd == -1)
    {
        perror("socket");
        return -1;
    }

    printf("Socket created successfully.\n");

    host_addrlen = sizeof(host_addr);

    host_addr.sin_family      = AF_INET;
    host_addr.sin_port        = htons(PORT);
    host_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(sockfd, (struct sockaddr *)&host_addr, host_addrlen) != 0)
    {
        perror("bind");
        close(sockfd);
        return -1;
    }

    printf("socket was bound successfully\n");

    if(listen(sockfd, SOMAXCONN) != 0)
    {
        perror("listen");
        close(sockfd);
        return -1;
    }

    printf("server listening for connections\n");

    return sockfd;
}

void set_socket_nonblock(int sockfd)
{
    int flags;
    flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

void set_fd_blocking(int fd)
{
    int flags;

    // Get the current flags
    flags = fcntl(fd, F_GETFL, 0);
    if(flags == -1)
    {
        perror("fcntl(F_GETFL) failed");
        return;
    }

    // Set the O_NONBLOCK flag to 0 (which makes the file descriptor blocking)
    if(fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1)
    {
        perror("fcntl(F_SETFL) failed");
    }
}

void handle_new_connection(int sockfd, int **client_sockets, nfds_t *max_clients, struct pollfd **fds)
{
    if((*fds)[0].revents & POLLIN)
    {
        socklen_t          addrlen;
        int                new_socket;
        int               *temp;
        struct sockaddr_in addr;

        addrlen    = sizeof(addr);
        new_socket = accept(sockfd, (struct sockaddr *)&addr, &addrlen);

        if(new_socket == -1)
        {
            perror("Accept error");
            exit(EXIT_FAILURE);
        }

        (*max_clients)++;
        temp = (int *)realloc(*client_sockets, sizeof(int) * (*max_clients));

        if(temp == NULL)
        {
            perror("realloc");
            free(*client_sockets);
            exit(EXIT_FAILURE);
        }
        else
        {
            struct pollfd *new_fds;
            *client_sockets                       = temp;
            (*client_sockets)[(*max_clients) - 1] = new_socket;

            new_fds = (struct pollfd *)realloc(*fds, (*max_clients + 1) * sizeof(struct pollfd));
            if(new_fds == NULL)
            {
                perror("realloc");
                free(*client_sockets);
                exit(EXIT_FAILURE);
            }
            else
            {
                *fds                        = new_fds;
                (*fds)[*max_clients].fd     = new_socket;
                (*fds)[*max_clients].events = POLLIN;
            }
        }

        printf("max clients: %d\n", (int)(*max_clients));
        for(int i = 0; i < (int)*max_clients; i++)
        {
            printf("client fd: %d\n", (*client_sockets)[i]);
        }
    }
}

void handle_client_disconnection(int **client_sockets, nfds_t *max_clients, struct pollfd **fds, nfds_t client_index)
{
    int disconnected_socket = (*client_sockets)[client_index];
    close(disconnected_socket);

    for(nfds_t i = client_index; i < *max_clients - 1; i++)
    {
        (*client_sockets)[i] = (*client_sockets)[i + 1];
    }

    (*max_clients)--;

    for(nfds_t i = client_index + 1; i <= *max_clients; i++)
    {
        (*fds)[i] = (*fds)[i + 1];
    }
}

void read_original_fd(int domain_socket, int **client_sockets, struct pollfd **fds, nfds_t *max_clients)
{
    int     fd_to_close;
    ssize_t bytes_read;

    set_socket_nonblock(domain_socket);

    bytes_read = read(domain_socket, &fd_to_close, sizeof(fd_to_close));

    if(bytes_read > 0)
    {
        for(nfds_t i = 0; i < *max_clients; i++)    // NOLINT
        {
            if((*fds)[i + 1].fd == fd_to_close)
            {
                handle_client_disconnection(client_sockets, max_clients, fds, i);
            }
        }
        return;
    }
}

void handle_client_data(struct pollfd *fds, const int *client_sockets, const nfds_t *max_clients, int domain_sock)
{
    for(nfds_t i = 0; i < *max_clients; i++)
    {
        if(*max_clients > 0 && client_sockets[i] != -1 && (fds[i + 1].revents & POLLIN))
        {
            send_fd(domain_sock, client_sockets[i]);
            fds[i + 1].events = 0;
        }
    }
}

void send_fd(int domain_socket, int fd)
{
    struct msghdr   msg = {0};    // holds both regular data and control data for passing file descriptors
    struct iovec    io;           // holds dummy single byte buffer
    int             data = fd;
    struct cmsghdr *cmsg;
    char            control[CMSG_SPACE(sizeof(int))];    // store file descriptor into control structure

    io.iov_base        = &data;
    io.iov_len         = sizeof(data);
    msg.msg_iov        = &io;
    msg.msg_iovlen     = 1;
    msg.msg_control    = control;
    msg.msg_controllen = sizeof(control);

    cmsg             = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type  = SCM_RIGHTS;
    cmsg->cmsg_len   = CMSG_LEN(sizeof(int));

    memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

    if(sendmsg(domain_socket, &msg, 0) < 0)
    {
        perror("sendmsg");
        exit(EXIT_FAILURE);
    }
}

int recv_fd(int socket, int *og_fd)
{
    struct msghdr   msg = {0};
    struct iovec    io;
    int             data = 0;
    struct cmsghdr *cmsg;
    char            control[CMSG_SPACE(sizeof(int))];
    int             fd;
    io.iov_base        = &data;
    io.iov_len         = sizeof(data);
    msg.msg_iov        = &io;
    msg.msg_iovlen     = 1;
    msg.msg_control    = control;
    msg.msg_controllen = sizeof(control);
    if(recvmsg(socket, &msg, 0) < 0)
    {
        if(errno != EINTR)
        {
            perror("recv");
        }
        exit(EXIT_FAILURE);
    }
    cmsg = CMSG_FIRSTHDR(&msg);
    if(cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
    {
        memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));

        if(og_fd)
        {
            *og_fd = data;
        }

        return fd;
    }
    return -1;
}

struct pollfd *initialize_pollfds(int sockfd, int **client_sockets)
{
    struct pollfd *fds;

    *client_sockets = NULL;

    fds = (struct pollfd *)malloc((1) * sizeof(struct pollfd));

    if(fds == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    fds[0].fd     = sockfd;
    fds[0].events = POLLIN;

    return fds;
}

void socket_close(int sockfd)
{
    if(close(sockfd) == -1)
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }

    printf("Socket closed.\n");
}

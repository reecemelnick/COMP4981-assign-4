#include "../include/network.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 8000
#define TIMEOUT 1000

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

// FIGURE OUT WHAT THIS DOES
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
    }
}

int accept_clients(int domain_sock, int server_sock, struct sockaddr_in client_addr, socklen_t client_addrlen)
{
    // const char hello[] = "sending socket";

    // poll server_socket for incoming data
    struct pollfd pfd = {server_sock, POLLIN, 0};

    while(1)
    {
        // poll to see if server has data to read
        int ret = poll(&pfd, 1, TIMEOUT);

        // if timeout expired, continue polling fd
        if(ret == 0)
        {
            continue;
        }

        // if error occured break loop
        if(ret < 0)
        {
            break;
        }

        // if poll output is POLLIN, accpect
        if(pfd.revents & POLLIN)
        {
            int newsockfd = accept(server_sock, (struct sockaddr *)&client_addr, &client_addrlen);
            if(newsockfd < 0)
            {
                perror("accept");
                break;
            }

            printf("Connection made\n");

            // write(domain_sock, hello, sizeof(hello));

            send_fd(domain_sock, newsockfd);
        }
    }

    return server_sock;
}

void send_fd(int domain_socket, int fd)
{
    struct msghdr   msg = {0};    // holds both regular data and control data for passing file descriptors
    struct iovec    io;           // holds dummy single byte buffer
    char            buf[1] = {0};
    struct cmsghdr *cmsg;
    char            control[CMSG_SPACE(sizeof(int))];    // store file descriptor into control structure

    io.iov_base        = buf;
    io.iov_len         = sizeof(buf);
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

int recv_fd(int socket)
{
    struct msghdr   msg = {0};
    struct iovec    io;
    char            buf[1];
    struct cmsghdr *cmsg;
    char            control[CMSG_SPACE(sizeof(int))];
    int             fd;
    io.iov_base        = buf;
    io.iov_len         = sizeof(buf);
    msg.msg_iov        = &io;
    msg.msg_iovlen     = 1;
    msg.msg_control    = control;
    msg.msg_controllen = sizeof(control);
    if(recvmsg(socket, &msg, 0) < 0)
    {
        exit(EXIT_FAILURE);
    }
    cmsg = CMSG_FIRSTHDR(&msg);
    if(cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
    {
        memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
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

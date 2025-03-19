#include "../include/network.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
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

    if(accept_clients(sockfd, host_addr, host_addrlen) < 0)
    {
        perror("Accept clients");
        return -1;
    }

    close(sockfd);
    return 0;
}

int accept_clients(int server_sock, struct sockaddr_in host_addr, socklen_t host_addrlen)
{
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
            int newsockfd = accept(server_sock, (struct sockaddr *)&host_addr, &host_addrlen);
            if(newsockfd < 0)
            {
                perror("accept");
                break;
            }

            printf("Connection made\n");
            close(newsockfd);
        }
    }

    return server_sock;
}
// #include "../include/database.h"
#include "../include/display.h"
#include "../include/network.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define N_WORKERS 3

int  socketfork(void);
int  parent(int socket);
void start_monitor(int socket);
void worker(int socket);

int main(void)
{
    int status;

    display("---Robust Server---");

    status = socketfork();
    if(status == -1)
    {
        perror("starting monitor");
    }

    sleep(2);    // NOLINT

    printf("exiting program...");
    return EXIT_SUCCESS;
}

void worker(int domain_socket)
{
    int     client_fd;
    ssize_t bytes_read;
    char    buffer[1024];    // NOLINT

    // char buffer[1024];    // NOLINT
    // while(1)
    // {
    //     ssize_t bytes_read;
    //     bytes_read = read(domain_socket, buffer, sizeof(buffer));
    //     if(bytes_read < 0)
    //     {
    //         perror("read fail");
    //         exit(EXIT_FAILURE);
    //     }
    //     if(bytes_read > 0)
    //     {
    //         printf("read in: %s\n", buffer);
    //     }
    // }

    client_fd = recv_fd(domain_socket);
    if(client_fd < 0)
    {
        perror("recv fd");
        exit(EXIT_FAILURE);
    }

    printf("recieved fd: %d\nWorker: %d\n", client_fd, (int)getpid());

    bytes_read = read(domain_socket, buffer, sizeof(buffer));
    if(bytes_read < 0)
    {
        perror("read fail");
        exit(EXIT_FAILURE);
    }
    if(bytes_read > 0)
    {
        printf("read in: %s\n", buffer);
    }
}

_Noreturn void start_monitor(int domain_socket)
{
    for(int i = 0; i < N_WORKERS; ++i)
    {
        int p = fork();
        if(p == 0)
        {
            worker(domain_socket);
            break;
        }
        if(p < 0)
        {
            perror("fork fail");
        }

        printf("process spawned pid: %d\n", p);
    }

    // MONITOR WORKER HEALTH
    while(1)
    {
    }
}

// TEST SOCKETPAIR. CHANGE TO MAIN SERVER LOGIC
int parent(int domain_socket)
{
    int                server_socket;
    struct sockaddr_in client_addr = {0};
    socklen_t          client_addrlen;

    // SETUP NETWORK SOCKET TO ACCEPT CLIENTS
    server_socket = initialize_socket();
    if(server_socket == -1)
    {
        perror("network socket");
        return -1;
    }

    client_addrlen = sizeof(client_addr);
    if(accept_clients(domain_socket, server_socket, client_addr, client_addrlen) < 0)
    {
        perror("Accept clients");
        return -1;
    }

    return 0;
}

// create monitor with a shared domain socket
int socketfork(void)
{
    int              fd[2];
    pid_t            pid;
    int              status;
    static const int parentsocket = 0;
    static const int childsocket  = 1;

    socketpair(PF_LOCAL, SOCK_STREAM, 0, fd);

    pid = fork();
    if(pid < 0)
    {
        perror("fork");
        return -1;
    }

    // handle parent and child
    if(pid == 0)
    {
        close(fd[parentsocket]);
        start_monitor(fd[childsocket]);
    }

    close(fd[childsocket]);
    status = parent(fd[parentsocket]);
    if(status == -1)
    {
        perror("parent");
        return -1;
    }

    return 0;
}

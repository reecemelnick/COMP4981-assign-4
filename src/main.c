#include "../include/database.h"
#include "../include/display.h"
#include "../include/network.h"
#include <arpa/inet.h>
#include <ndbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define N_WORKERS 3

int  socketfork(void);
void parent(int socket);
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

    printf("handle main server process\n");

    // SETUP NETWORK SOCKET TO ACCEPT CLIENTS
    status = initialize_socket();
    if(status == -1)
    {
        perror("network socket");
    }

    return EXIT_SUCCESS;
}

_Noreturn void worker(int socket)
{
    char buffer[1024];    // NOLINT
    while(1)
    {
        ssize_t bytes_read;
        bytes_read = read(socket, buffer, sizeof(buffer));
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
}

_Noreturn void start_monitor(int socket)
{
    for(int i = 0; i < N_WORKERS; ++i)
    {
        int p = fork();
        if(p == 0)
        {
            worker(socket);
            break;
        }
        if(p < 0)
        {
            perror("fork fail");
        }

        printf("process spawned pid: %d\n", p);
    }

    while(1)
    {
    };
}

// TEST SOCKETPAIR. CHANGE TO MAIN SERVER LOGIC
void parent(int socket)
{
    const char hello[] = "sending socket";
    sleep(2);    // NOLINT
    write(socket, hello, sizeof(hello));
}

// create monitor with a shared domain socket
int socketfork(void)
{
    int              fd[2];
    pid_t            pid;
    static const int parentsocket = 0;
    static const int childsocket  = 1;

    socketpair(PF_LOCAL, SOCK_STREAM, 0, fd);

    pid = fork();
    if(pid == 0)
    {
        close(fd[parentsocket]);
        start_monitor(fd[childsocket]);
    }
    else if(pid > 0)
    {
        close(fd[childsocket]);
        parent(fd[parentsocket]);
    }
    else
    {
        perror("fork");
        return -1;
    }

    return 0;
}

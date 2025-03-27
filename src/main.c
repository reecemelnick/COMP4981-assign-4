// #include "../include/database.h"
#include "../include/display.h"
#include "../include/network.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define N_WORKERS 3

int         socketfork(void);
int         parent(int socket);
void        start_monitor(int socket);
void        worker(int socket);
static void setup_signal_handler(void);
static void sigint_handler(int signum);

static volatile sig_atomic_t exit_flag = 0;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

int main(void)
{
    int status;

    display("---Robust Server---");

    setup_signal_handler();

    status = socketfork();
    if(status == -1)
    {
        perror("starting monitor");
    }

    printf("exiting program...\n");
    sleep(1);    // NOLINT

    return EXIT_SUCCESS;
}

_Noreturn void worker(int domain_socket)
{
    set_fd_blocking(domain_socket);
    while(!exit_flag)
    {
        int client_fd;
        int original_fd;
        client_fd = recv_fd(domain_socket, &original_fd);
        if(client_fd < 0)
        {
            perror("recv fd");
            exit(EXIT_FAILURE);
        }

        if(client_fd > 0)
        {
            while(1)
            {
                int work_done;
                work_done = worker_handle_client(client_fd);
                if(work_done == -1)
                {
                    break;
                }
                sleep(1);
            }
        }
        close(client_fd);
        write(domain_socket, &original_fd, sizeof(original_fd));    // send int of fd to close in parent
    }

    printf("Worker exiting...\n");
    exit(EXIT_SUCCESS);
}

_Noreturn void start_monitor(int domain_socket)
{
    for(int i = 0; i < N_WORKERS; ++i)
    {
        int p = fork();
        if(p == 0)
        {
            worker(domain_socket);
        }
        if(p < 0)
        {
            perror("fork fail");
        }

        printf("process spawned pid: %d\n", p);
    }

    // MONITOR WORKER HEALTH
    while(!exit_flag)
    {
        sleep(1);    // NOLINT
    }

    exit(EXIT_SUCCESS);
}

// TEST SOCKETPAIR. CHANGE TO MAIN SERVER LOGIC
int parent(int domain_socket)
{
    int server_socket;

    int           *client_sockets = NULL;
    nfds_t         max_clients    = 0;
    struct pollfd *fds;    // keeping track of all fds

    // SETUP NETWORK SOCKET TO ACCEPT CLIENTS
    server_socket = initialize_socket();
    if(server_socket == -1)
    {
        perror("network socket");
        return -1;
    }

    fds = initialize_pollfds(server_socket, &client_sockets);

    set_socket_nonblock(domain_socket);

    while(!exit_flag)
    {
        int activity;

        // poll for connection attempt
        activity = poll(fds, max_clients + 1, 1000);    //  NOLINT
        if(activity < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }

            perror("Poll error");
            exit(EXIT_FAILURE);
        }

        // TEST CONNECTIONS
        handle_new_connection(server_socket, &client_sockets, &max_clients, &fds);

        if(client_sockets != NULL)
        {
            // Handle incoming data from existing clients
            // IF INCOMING DATA SEND FILE DESCRIPTOR TO WORKER
            handle_client_data(fds, client_sockets, &max_clients, domain_socket);
        }
    }

    free(fds);

    // Cleanup and close all client sockets
    for(size_t i = 0; i < max_clients; i++)
    {
        if(client_sockets != NULL)
        {
            if(client_sockets[i] > 0)
            {
                socket_close(client_sockets[i]);
            }
        }
    }

    free(client_sockets);
    socket_close(server_socket);

    return 0;
}

// create monitor with a shared domain socket
int socketfork(void)
{
    // int              fd[2];
    int   fd;
    int   client_socket;
    pid_t pid;
    // static const int parentsocket = 0;
    // static const int childsocket  = 1;

    // socketpair(PF_LOCAL, SOCK_STREAM, 0, fd);

    fd = create_domain_socket();

    pid = fork();

    // handle parent and child
    if(pid == 0)
    {
        close(fd);
        client_socket = connect_to_domain();
        if(client_socket < 0)
        {
            perror("Failed to connect in child");
            exit(EXIT_FAILURE);
        }
        start_monitor(client_socket);
        close(client_socket);
    }
    else if(pid > 0)
    {
        client_socket = accept(fd, NULL, NULL);
        if(client_socket == -1)
        {
            perror("accept");
            close(fd);
            exit(EXIT_FAILURE);
        }

        parent(client_socket);
        close(client_socket);
    }

    if(pid < 0)
    {
        perror("fork");
        return -1;
    }

    close(fd);
    return 0;
}

static void setup_signal_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif
    sa.sa_handler = sigint_handler;
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif
    sigaction(SIGINT, &sa, NULL);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static void sigint_handler(int signum)
{
    exit_flag = 1;
}

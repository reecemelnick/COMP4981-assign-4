// #include "../include/database.h"
#include "../include/display.h"
#include "../include/network.h"
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
        printf("recieved fd: %d\nWorker: %d\n", client_fd, (int)getpid());
        printf("OG FD: %d\n", original_fd);

        if(client_fd > 0)
        {
            while(1)
            {
                int work_done;
                work_done = worker_handle_client(client_fd);
                if(work_done == -1)
                {
                    printf("work done\n");
                    break;
                }

                sleep(1);
            }
        }
        close(client_fd);
        write(domain_socket, &original_fd, sizeof(original_fd));    // send int of fd to close in parent
    }

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
            // break;
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

    printf("Domain socket: %d\n", domain_socket);

    printf("Polling for incoming connections...\n");
    fds = initialize_pollfds(server_socket, &client_sockets);
    printf("fds init...\n");

    set_socket_nonblock(domain_socket);

    while(!exit_flag)
    {
        int     activity;
        int     fd_to_close;
        ssize_t bytes_read;

        sleep(1);

        // poll for connection attempt
        activity = poll(fds, max_clients + 1, -1);
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

        sleep(3);    // NOLINT

        bytes_read = read(domain_socket, &fd_to_close, sizeof(fd_to_close));
        if(bytes_read > 0)
        {
            printf("got of fd back: %d\n", fd_to_close);
            for(nfds_t i = 0; i <= max_clients; i++)    // NOLINT
            {
                if(fds[i].fd == fd_to_close)
                {
                    printf("gonna DELETE\n");
                    handle_client_disconnection(&client_sockets, &max_clients, &fds, (i - 1));
                }
            }

            printf("size of client fd: %d\n", (int)max_clients);
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
    int              fd[2];
    pid_t            pid;
    static const int parentsocket = 0;
    static const int childsocket  = 1;

    socketpair(PF_LOCAL, SOCK_STREAM, 0, fd);

    pid = fork();

    // handle parent and child
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

    if(pid < 0)
    {
        perror("fork");
        return -1;
    }

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

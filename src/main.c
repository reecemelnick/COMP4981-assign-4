// #include "../include/database.h"
#include "../include/display.h"
#include "../include/network.h"
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define N_WORKERS 3

int         socketfork(void);
int         parent(int socket);
void        start_monitor(int socket);
void        worker(int socket);
static void setup_signal_handler(void);
static void sigint_handler(int signum);
int (*load_lib(void **handle, const char *lib_path))(int);
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

int (*load_lib(void **handle, const char *lib_path))(int)
{
    union
    {
        void *ptr;
        int (*func)(int);
    } cast_helper;

    int (*worker_handle_so)(int) = NULL;

    *handle = dlopen(lib_path, RTLD_LAZY);
    if(*handle == NULL)
    {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return NULL;
    }

    cast_helper.ptr  = dlsym(*handle, "worker_handle_so");
    worker_handle_so = cast_helper.func;
    // worker_handle_so = (int (*)(int))dlsym(*handle, "worker_handle_so");

    if(!worker_handle_so)
    {
        fprintf(stderr, "dlsym failed for worker_handle_so: %s\n", dlerror());
        dlclose(*handle);
        return NULL;
    }

    return worker_handle_so;
}

void worker(int domain_socket)
{
    void *handle;
    int (*worker_handle)(int);
    struct stat lib_stat;
    struct stat prev_lib_stat;
    const char *lib_path = "/Users/reecemelnick/Desktop/COMP4981/assign4/src/libmylib.so";

    if(stat(lib_path, &prev_lib_stat) == -1)
    {
        perror("stat failed");
        return;
    }

    worker_handle = load_lib(&handle, lib_path);
    if(!worker_handle)
    {
        exit(EXIT_FAILURE);
    }

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

        if(stat(lib_path, &lib_stat) == -1)
        {
            perror("stat failed");
            break;
        }

        if(lib_stat.st_mtime != prev_lib_stat.st_mtime)
        {
            printf("Library updated. Reloading...\n");

            dlclose(handle);
            worker_handle = load_lib(&handle, lib_path);
            if(!worker_handle)
            {
                break;
            }

            prev_lib_stat = lib_stat;
        }

        if(client_fd > 0)
        {
            while(1)
            {
                int work_done;
                work_done = worker_handle(client_fd);

                if(work_done == 0)
                {
                    printf("DONE: %d\n", getpid());
                    printf("sending: %d\n", original_fd);    // NOLINT
                    write(domain_socket, &original_fd, sizeof(original_fd));
                    close(client_fd);    // send int of fd to close in parent
                }
                if(work_done == -1)
                {
                    break;
                }
            }
        }
        close(client_fd);    // NOLINT
    }
    dlclose(handle);
    printf("Worker exiting...\n");
    exit(EXIT_SUCCESS);
}

_Noreturn void start_monitor(int domain_socket)
{
    pid_t workers[N_WORKERS];

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

        workers[i] = p;
        printf("process spawned pid: %d\n", p);
    }

    // MONITOR WORKER HEALTH
    while(!exit_flag)
    {
        for(int i = 0; i < N_WORKERS; ++i)
        {
            int   status;
            pid_t result = waitpid(workers[i], &status, WNOHANG);

            if(result == -1)
            {
                perror("waitpid failed");
                continue;
            }

            if(result == 0)
            {
                continue;
            }

            // worker killed, spawn new
            if(WIFEXITED(status) || WIFSIGNALED(status))
            {
                pid_t p;
                printf("Worker %d exited, restarting worker...\n", workers[i]);
                sleep(5);    // NOLINT
                p = fork();
                if(p == 0)
                {
                    worker(domain_socket);
                    exit(EXIT_SUCCESS);
                }
                if(p < 0)
                {
                    perror("fork failed");
                    exit(EXIT_FAILURE);
                }

                workers[i] = p;
                printf("Re-spawned worker with pid: %d\n", p);
            }
        }

        sleep(1);    // NOLINT
    }

    close(domain_socket);
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
        read_original_fd(domain_socket, &client_sockets, &fds, &max_clients);
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
    int   fd;
    int   client_socket;
    pid_t pid;

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

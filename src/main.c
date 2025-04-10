#include "../include/network.h"
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <semaphore.h>
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

#define MIN_WORKERS 1
#define MAX_WORKERS 5
#define BASE 10

int         socketfork(int workers_num);
int         parent(int socket);
void        start_monitor(int socket, int workers_num);
void        worker(int socket, sem_t *semaphore);
static void setup_signal_handler(void);
static void sigint_handler(int signum);
int (*load_lib(void **handle, const char *lib_path))(int, sem_t *);
void handle_arguments(int argc, char *argv[], int *workers_num);

static volatile sig_atomic_t exit_flag = 0;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

int main(int argc, char *argv[])
{
    int status;
    int workers_num = 0;

    setup_signal_handler();

    handle_arguments(argc, argv, &workers_num);
    if(!workers_num)
    {
        printf("Select number of workers -w <num>. Must be an integer between %d and %d.\n", MIN_WORKERS, MAX_WORKERS);
        exit(EXIT_FAILURE);
    }

    status = socketfork(workers_num);
    if(status == -1)
    {
        perror("starting monitor");
    }

    printf("exiting program...\n");
    sleep(1);

    return EXIT_SUCCESS;
}

int (*load_lib(void **handle, const char *lib_path))(int, sem_t *)
{
    // avoiding direct casting
    union
    {
        void *ptr;
        int (*func)(int, sem_t *);
    } cast_helper;

    int (*worker_handle_so)(int, sem_t *) = NULL;

    *handle = dlopen(lib_path, RTLD_LAZY);
    if(*handle == NULL)
    {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return NULL;
    }

    cast_helper.ptr  = dlsym(*handle, "worker_handle_so");
    worker_handle_so = cast_helper.func;

    if(!worker_handle_so)
    {
        fprintf(stderr, "dlsym failed for worker_handle_so: %s\n", dlerror());
        dlclose(*handle);
        return NULL;
    }

    return worker_handle_so;
}

void worker(int domain_socket, sem_t *semaphore)
{
    void *handle;
    int (*worker_handle)(int, sem_t *);
    struct stat lib_stat;
    struct stat prev_lib_stat;
    const char *lib_path = "/Users/developer/rm4/src/libmylib.so";

    // initially set prev_lib_stat so we can compare changes
    if(stat(lib_path, &prev_lib_stat) == -1)
    {
        perror("stat failed");
        return;
    }

    // load shared object entry function
    worker_handle = load_lib(&handle, lib_path);
    if(!worker_handle)
    {
        exit(EXIT_FAILURE);
    }

    while(!exit_flag)
    {
        int client_fd;
        int original_fd;
        client_fd = recv_fd(domain_socket, &original_fd);    // request was given at this point
        if(client_fd < 0)
        {
            perror("recv fd");
            exit(EXIT_FAILURE);
        }

        // get new stat when request was given
        if(stat(lib_path, &lib_stat) == -1)
        {
            perror("stat failed");
            break;
        }

        // if time of last update was changed, reload the library
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

        // on successfully recieved file descriptor
        if(client_fd > 0)
        {
            while(1)
            {
                int work_done;
                work_done = worker_handle(client_fd, semaphore);
                // work was completed by the worker
                if(work_done == 0)
                {
                    // write back the fd to be closed
                    write(domain_socket, &original_fd, sizeof(original_fd));
                    close(client_fd);
                }
                if(work_done == -1)
                {
                    break;
                }
            }
        }
        close(client_fd);
    }
    dlclose(handle);
    exit(EXIT_SUCCESS);
}

_Noreturn void start_monitor(int domain_socket, int workers_num)
{
    pid_t *workers;
    sem_t *semaphore;

    if(workers_num <= 0)
    {
        fprintf(stderr, "workers_num must be greater than 0\n");
        exit(EXIT_FAILURE);
    }

    workers = (pid_t *)malloc(sizeof(pid_t) * (size_t)workers_num);
    if(workers == NULL)
    {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }

    semaphore = sem_open("/db_sem", O_CREAT, 0644, 1);    // NOLINT
    if(semaphore == SEM_FAILED)
    {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }

    for(int i = 0; i < workers_num; ++i)
    {
        int p = fork();
        if(p == 0)
        {
            worker(domain_socket, semaphore);
        }
        if(p < 0)
        {
            perror("fork fail");
            exit(EXIT_FAILURE);
        }

        workers[i] = p;
        printf("process spawned pid: %d\n", p);
    }

    // MONITOR WORKER HEALTH
    while(!exit_flag)
    {
        sleep(1);
        for(int i = 0; i < workers_num; ++i)
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
                printf("worker %d failed, spawning new...\n", workers[i]);
                sleep(3);    // NOLINT
                p = fork();
                if(p == 0)
                {
                    worker(domain_socket, semaphore);
                }
                if(p < 0)
                {
                    perror("fork failed");
                    exit(EXIT_FAILURE);
                }

                workers[i] = p;
                printf("spawned worker with pid: %d\n", p);
            }
        }
    }

    close(domain_socket);
    free(workers);
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

int socketfork(int workers_num)
{
    int   sv[2];
    pid_t pid;

    if(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1)
    {
        perror("socketpair");
        return -1;
    }

    pid = fork();

    if(pid == 0)
    {
        close(sv[0]);
        start_monitor(sv[1], workers_num);
    }
    else if(pid > 0)
    {
        close(sv[1]);
        parent(sv[0]);
    }
    else
    {
        perror("fork");
        return -1;
    }

    return 0;
}

void handle_arguments(int argc, char *argv[], int *workers_num)
{
    int option;
    while((option = getopt(argc, argv, "w:")) != -1)
    {
        if(option == 'w')
        {
            long  val;
            char *endptr;
            errno = 0;
            val   = strtol(optarg, &endptr, BASE);

            if(errno != 0 || *endptr != '\0' || val < MIN_WORKERS || val > MAX_WORKERS)
            {
                printf("must be an integer between %d and %d.\n", MIN_WORKERS, MAX_WORKERS);
                exit(EXIT_FAILURE);
            }

            *workers_num = (int)val;
        }
        else
        {
            perror("Error invalid command line args");
            exit(EXIT_FAILURE);
        }
    }
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

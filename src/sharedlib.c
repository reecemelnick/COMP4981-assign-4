#include "../include/sharedlib.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <ndbm.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_SIZE 4096
#define TIME_BUFFER 64

#define OK_STATUS 200
#define FILE_NOT_FOUND 404
#define PERMISSION_DENIED 403

void my_function(void)
{
    printf("Hello from the shared library!\n");
}

int worker_handle_so(int client_sock)
{
    ssize_t valread;
    int     retval;
    char    method[BUFFER_SIZE];
    char    uri[BUFFER_SIZE];
    char    version[BUFFER_SIZE];
    char    buffer[BUFFER_SIZE];

    memset(buffer, 0, BUFFER_SIZE);

    valread = read(client_sock, buffer, BUFFER_SIZE);

    if(valread <= 0)
    {
        // Connection closed or error
        return -1;
    }

    sscanf(buffer, "%15s %255s %15s", method, uri, version);
    printf("%s %s %s\n", method, uri, version);

    retval = verify_method(method);
    if(retval == -1)
    {
        const char *error_message = "<html><body><h1>405 Method Not Allowed</h1></body></html>";
        form_response(client_sock, "405 Method Not Allowed", (int)strlen(error_message), "text/html");
        write(client_sock, error_message, strlen(error_message));
        return 0;
    }

    if(strcmp(method, "POST") == 0)
    {
        printf("BUF: %s\n", buffer);
        retval = handle_post_request(uri, client_sock, buffer);
        return retval;
    }

    retval = check_http_format(version, uri);
    if(retval == -1)
    {
        if(strcmp(method, "GET") == 0)
        {
            const char *error_message = "<html><body><h1>400 Bad Request</h1></body></html>";
            form_response(client_sock, "400 Bad Request", (int)strlen(error_message), "text/html");
            write(client_sock, error_message, strlen(error_message));
        }
        else if(strcmp(method, "HEAD") == 0)
        {
            form_response(client_sock, "400 Bad Request", 0, "text/html");
        }
        return 0;
    }

    // GET FROM DATABASE
    if(strncmp(uri, "/dataGET?key=", 13) == 0)    // NOLINT
    {
        retval = fetch_entry(uri, method, client_sock);
        return retval;
    }

    // GET FROM FILES
    if(strcmp(uri, "/") == 0)
    {
        snprintf(uri, sizeof(uri), "/index.html");
    }

    retval = serve_file(uri, method, client_sock);
    if(retval != OK_STATUS)
    {
        if(retval == FILE_NOT_FOUND)
        {
            if(strcmp(method, "GET") == 0)
            {
                const char *error_message = "<html><body><h1>404 Not Found</h1></body></html>";
                form_response(client_sock, "404 Not Found", (int)strlen(error_message), "text/html");
                write(client_sock, error_message, strlen(error_message));
            }
            else if(strcmp(method, "HEAD") == 0)
            {
                form_response(client_sock, "404 Not Found", 0, "text/html");
            }
        }

        if(retval == PERMISSION_DENIED)
        {
            if(strcmp(method, "GET") == 0)
            {
                const char *error_message = "<html><body><h1>403 Forbidden</h1></body></html>";
                form_response(client_sock, "403 Forbidden", (int)strlen(error_message), "text/html");
                write(client_sock, error_message, strlen(error_message));
            }
            else if(strcmp(method, "HEAD") == 0)
            {
                form_response(client_sock, "403 Forbidden", 0, "text/html");
            }
        }
        return 0;
    }

    return 0;
}

int add_to_db(char *key_str, char *value_str, size_t value_len)
{
    DBM  *db;
    datum key;
    datum value;

    db = dbm_open("/Users/reecemelnick/Desktop/COMP4981/assign4/database", O_RDWR | O_CREAT, 0644);    // NOLINT
    if(db == NULL)
    {
        perror("Opening NDBM database");
        return -1;
    }

    printf("gonnan make:\n");

    key.dptr    = (void *)key_str;
    key.dsize   = strlen(key_str);
    value.dptr  = (void *)value_str;
    value.dsize = value_len;

    if(dbm_store(db, key, value, DBM_INSERT) != 0)
    {
        perror("Error storing data in NDBM");
        dbm_close(db);
        return -1;
    }

    dbm_close(db);
    return 0;
}

int fetch_entry(const char *uri, const char *method, int client_sock)
{
    char key[64];    // NOLINT
    char value[BUFFER_SIZE];
    strncpy(key, uri + 13, sizeof(key) - 1);    // NOLINT
    key[sizeof(key) - 1] = '\0';

    if(find_in_db(key, value, sizeof(value)) == 0)
    {
        char response_body[BUFFER_SIZE];
        snprintf(response_body, sizeof(response_body), "{\"key\": \"%s\", \"value\": \"%s\"}", key, value);

        if(strcmp(method, "GET") == 0)
        {
            form_response(client_sock, "200 OK", (int)strlen(response_body), "application/json");
            write(client_sock, response_body, strlen(response_body));
        }
        else if(strcmp(method, "HEAD") == 0)
        {
            form_response(client_sock, "200 OK", 0, "application/json");
        }
    }
    else
    {
        if(strcmp(method, "GET") == 0)
        {
            const char *error_message = "<html><body><h1>404 Not Found</h1></body></html>";
            form_response(client_sock, "404 Not Found", (int)strlen(error_message), "text/html");
            write(client_sock, error_message, strlen(error_message));
        }
        else if(strcmp(method, "HEAD") == 0)
        {
            form_response(client_sock, "404 Not Found", 0, "text/html");
        }
    }

    return 0;
}

int find_in_db(char *key_str, char *returned_value, size_t max_len)
{
    DBM   *db;
    datum  key;
    datum  value;
    size_t copy_len;

    db = dbm_open("/Users/reecemelnick/Desktop/COMP4981/assign4/database", O_RDONLY, 0644);    // NOLINT   // Open as read-only
    if(db == NULL)
    {
        perror("Opening NDBM database");
        return -1;
    }

    key.dptr  = (void *)key_str;
    key.dsize = strlen(key_str);

    value = dbm_fetch(db, key);
    if(value.dptr == NULL)
    {
        dbm_close(db);
        return -1;
    }

    // either actual size of max size
    copy_len = (value.dsize < max_len - 1) ? value.dsize : max_len - 1;
    memcpy(returned_value, value.dptr, copy_len);
    returned_value[copy_len] = '\0';

    dbm_close(db);
    return 0;
}

void read_all_entries(const char *db_file)
{
    DBM  *db;
    datum key;

    db = dbm_open(db_file, O_RDONLY, 0);
    if(db == NULL)
    {
        perror("Error opening database");
        return;
    }

    key = dbm_firstkey(db);
    while(key.dptr != NULL)
    {
        datum value;
        value = dbm_fetch(db, key);

        printf("Key: ");
        fwrite(key.dptr, 1, key.dsize, stdout);
        printf(", Value: ");
        fwrite(value.dptr, 1, value.dsize, stdout);
        printf("\n");

        key = dbm_nextkey(db);
    }

    dbm_close(db);
}

int handle_post_request(const char *uri, int client_sock, char *request_body)
{
    char        response_body[BUFFER_SIZE];
    char        content_length_str[32];    // NOLINT
    char        key_str[32];               // NOLINT
    long        content_length = 0;
    char       *body_start;
    size_t      body_length;
    const char *content_length_header;

    content_length_header = strstr(request_body, "Content-Length:");
    if(content_length_header)
    {
        char *endptr;
        content_length = strtol(content_length_header + 15, &endptr, 10);    // NOLINT
        if(*endptr != '\0' && *endptr != '\r' && *endptr != '\n')
        {
            fprintf(stderr, "Invalid Content-Length header format\n");
            form_response(client_sock, "400 Bad Request", 0, "text/plain");
            return 0;
        }
    }
    else
    {
        printf("null\n");
        form_response(client_sock, "400 Bad Request", 0, "text/plain");
        return 0;
    }

    if(strcmp(uri, "/dataPOST") != 0)
    {
        form_response(client_sock, "404 Not Found", 0, "text/plain");
        return 0;
    }

    body_start = strstr(request_body, "\r\n\r\n");
    if(body_start)
    {
        body_start += 4;    // NOLINT
    }
    else
    {
        printf("body\n");

        form_response(client_sock, "400 Bad Request", 0, "text/plain");
        return 0;
    }

    body_length = strlen(body_start);
    if(body_length != (size_t)content_length)
    {
        fprintf(stderr, "mismatch: expected %ld, got %zu\n", content_length, body_length);
        form_response(client_sock, "400 Bad Request", 0, "text/plain");
        return 0;
    }

    snprintf(key_str, sizeof(key_str), "%ld", time(NULL));    // Use the current time as the key

    if(add_to_db(key_str, body_start, body_length) != 0)
    {
        form_response(client_sock, "500 Internal Server Error", 0, "text/plain");
        return -1;
    }

    snprintf(response_body, sizeof(response_body), "{\"message\": \"Data received successfully\"}");
    snprintf(content_length_str, sizeof(content_length_str), "%d", (int)(strlen(response_body)));

    form_response(client_sock, "200 OK", (int)strlen(response_body), "application/json");
    write(client_sock, response_body, strlen(response_body));

    read_all_entries("/Users/reecemelnick/Desktop/COMP4981/assign4/database");

    return 0;
}

void form_response(int newsockfd, const char *status, int content_length, const char *content_type)
{
    // const char *time_buffer;            // buffer to store readable time
    struct tm tm_result;              // time structure
    char      header[BUFFER_SIZE];    // buffer to hold contents of response
    char      timestamp[TIME_BUFFER];

    get_http_date(&tm_result);    // get current time

    format_time(tm_result, timestamp);    // format time to human readable string

    // format response header for status 200 OK
    snprintf(header,
             sizeof(header),
             "HTTP/1.1 %s\r\n"    // Update to HTTP/1.1
             "Server: HTTPServer/1.0\r\n"
             "Date: %s\r\n"
             "Connection: close\r\n"
             "Content-Length: %d\r\n"
             "Content-Type: %s\r\n\r\n",
             status,
             timestamp,
             content_length,
             content_type);

    printf("%s\n", header);
    write(newsockfd, header, strlen(header));    // send response to client
    fflush(stdout);
}

int verify_method(const char *method)
{
    if(strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0 && strcmp(method, "POST") != 0)
    {
        return -1;
    }

    return 0;
}

void get_http_date(struct tm *result)
{
    time_t now = time(NULL);
    if(localtime_r(&now, result) == NULL)
    {
        perror("localtime_r");
    }
}

void format_time(struct tm tm_result, char *time_buffer)
{
    if(strftime(time_buffer, TIME_BUFFER, "%a, %d %b %Y %H:%M:%S GMT", &tm_result) == 0)
    {
        printf("could not format time");
    }
}

const char *get_content_type(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if(!ext)
    {
        return "application/octet-stream";
    }

    if(strcmp(ext, ".html") == 0)
    {
        return "text/html";
    }
    if(strcmp(ext, ".css") == 0)
    {
        return "text/css";
    }
    if(strcmp(ext, ".js") == 0)
    {
        return "application/javascript";
    }
    if(strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
    {
        return "image/jpeg";
    }
    if(strcmp(ext, ".png") == 0)
    {
        return "image/png";
    }
    if(strcmp(ext, ".gif") == 0)
    {
        return "image/gif";
    }
    if(strcmp(ext, ".swf") == 0)
    {
        return "application/x-shockwave-flash";
    }

    return "application/octet-stream";
}

int check_http_format(const char *version, const char *uri)
{
    // verify version
    if(strcmp(version, "HTTP/1.1") != 0)
    {
        printf("not correct version\n");
        return -1;
    }

    // verify uri
    if(strstr(uri, "..") != NULL)
    {
        printf("invalid uri\n");
        return -1;
    }

    printf("Format is good\n");

    return 0;
}

int serve_file(const char *uri, const char *method, int client_sock)
{
    char filepath[BUFFER_SIZE];
    int  retval;

    snprintf(filepath, sizeof(filepath), "/Users/reecemelnick/Desktop/COMP4981/assign4/public/%s", uri);

    retval = check_file_status(filepath);
    if(retval != OK_STATUS)
    {
        return retval;
    }

    retval = read_file(filepath, method, client_sock);
    {
        if(retval == -1)
        {
            return -1;
        }
    }

    return 0;
}

// check if requested resource is a directory using stat
int is_directory(const char *filepath)
{
    struct stat file_stat;

    if(stat(filepath, &file_stat) == -1)
    {
        perror("stat");
        return -1;
    }

    if(S_ISDIR(file_stat.st_mode))
    {
        return 0;
    }

    return -1;
}

// check if requested resource exists and has appropriate permission
int check_file_status(char *filepath)
{
    struct stat file_stat;
    int         status_code;

    // printf("filepath: %s\n", filepath);

    if(stat(filepath, &file_stat) == 0 && S_ISREG(file_stat.st_mode))
    {
        if(access(filepath, R_OK) == 0)
        {
            status_code = OK_STATUS;
        }
        else
        {
            status_code = PERMISSION_DENIED;
        }
    }
    else if(access(filepath, F_OK) == -1)
    {
        status_code = FILE_NOT_FOUND;
    }
    else
    {
        status_code = PERMISSION_DENIED;
    }

    printf("status: %d\n", status_code);

    return status_code;
}

int read_file(const char *filepath, const char *method, int client_socket)
{
    int     filefd;
    char    file_buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    // SUCCESS HEADER
    if(strcmp(method, "GET") == 0)
    {
        form_response(client_socket, "200 OK", get_file_size(filepath), get_content_type(filepath));
    }
    else if(strcmp(method, "HEAD") == 0)
    {
        form_response(client_socket, "200 OK", 0, get_content_type(filepath));
        return 0;
    }

    filefd = open(filepath, O_RDONLY | O_CLOEXEC);
    if(filefd < 0)
    {
        perror("opening file");
        return -1;
    }

    while((bytes_read = read(filefd, file_buffer, sizeof(file_buffer))) > 0)
    {
        write(client_socket, file_buffer, (size_t)bytes_read);
    }

    close(filefd);

    return 0;
}

// returns content length of file
int get_file_size(const char *filepath)
{
    struct stat file_stat;

    if(stat(filepath, &file_stat) == 0)
    {
        return (int)file_stat.st_size;
    }

    perror("stat");
    return -1;
}

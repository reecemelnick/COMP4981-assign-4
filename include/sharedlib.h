#include <time.h>

#ifndef MYLIB_H
    #define MYLIB_H
void my_function(void);
#endif

int         worker_handle_so(int client_sock);
void        get_http_date(struct tm *result);
int         check_http_format(const char *version, const char *uri);
int         serve_file(const char *uri, const char *method, int client_sock);
int         check_file_status(char *filepath);
int         read_file(const char *filepath, const char *method, int client_socket);
const char *get_content_type(const char *filename);
int         verify_method(const char *method);
void        form_response(int newsockfd, const char *status, int content_length, const char *content_type);
void        format_time(struct tm tm_result, char *time_buffer);
int         is_directory(const char *filepath);
int         get_file_size(const char *filepath);

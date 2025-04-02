
#include <semaphore.h>
#include <time.h>

#ifndef MYLIB_H
    #define MYLIB_H
void my_function(void);
#endif

int         worker_handle_so(int client_sock, sem_t *sem);
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
int         handle_post_request(const char *uri, int client_sock, char *request_body, sem_t *semaphore);
int         add_to_db(const char *key_str, const char *value_str);
void        read_all_entries(void);
int         find_in_db(const char *key_str, char *returned_value, size_t max_len);
int         fetch_entry(const char *uri, const char *method, int client_sock, sem_t *semaphore);
void        handle_file_serve_error(const char *method, int retval, int client_sock);
void        handle_verify_method_error(int client_sock);
void        handle_check_format_error(const char *method, int client_sock);
void        handle_file_not_found(const char *method, int client_sock);
void        handle_forbidden(const char *method, int client_sock);
char       *parse_value(char *body_start);
char       *parse_key(char *body_start);

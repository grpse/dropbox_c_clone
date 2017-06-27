#ifndef DROPBOXCLIENT_H
#define DROPBOXCLIENT_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <limits.h>
#include <pwd.h>

#define __USE_XOPEN
#include <time.h>
#include <utime.h>
#include <signal.h>
#include <unistd.h>

#include "packager.h"
#include "dropboxUtil.h"

// Macros para o inotify
#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (1024 * (EVENT_SIZE + 16))

// String Macros
#define UPLOAD "upload"
#define LIST "list"
#define DOWNLOAD "download"
#define GET_SYNC_DIR "get_sync_dir"
#define DELETE "delete"
#define EXIT "exit"
#define EXIST "exist"

// Protótipos mínimos
int connect_server(char *host, int port);
int login(char *username);
int get_file(char *filename);
int sync_client(char *username);
int list_files();
int send_file(char *filename);
int delete_file(char *filename);
void finalize_thread_and_close_connection(int exit_code);
int close_connection();

// Protótipos de funções auxiliares
int is_list_command(char *command_buffer);
int is_delete_command(char *command_buffer);
int is_download_command(char *command_buffer);
int is_upload_command(char *command_buffer);
int is_get_sync_dir_command(char *command_buffer);
int is_exit_command(char *command_buffer);
int file_copy_to_sync_dir(char* source_file_path, char* dest_file_name);
int file_remove_from_sync_dir(char* file_name);

// Protótipos para a sincronização dos arquivos
int exist_local_sync_dir();
int start_sync_monitor();
int first_sync_local_files(char *user_path);
void *file_sync_monitor(void *);
void get_sync_dir_local_path(char *out_user_path);
void *auto_sync_files(void *);

// Faz uma requisição de hora ao time server na porta + 1
time_t get_time_server();

#endif /*DROPBOXCLIENT_H*/
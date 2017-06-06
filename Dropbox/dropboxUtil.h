#ifndef DROPBOXUTIL_H
#define DROPBOXUTIL_H

#include <time.h>
#include <limits.h>

#define USERNAME_MAX 64
#define MESSAGE_MAX 64
#define MAX_USERID 64
#define MAXFILES 256

#define MAX_SAME_USER 2

#define PORT 9000
#define BUF_SIZE 2048

// Mutex scope lock
#define SCOPELOCK(scope_mutex, scope)         \
    {                                         \
        pthread_mutex_lock((&scope_mutex));    \
        {scope;};                             \
        pthread_mutex_unlock((&scope_mutex));  \
    } \

struct file_info {
  char name[MAX_USERID];
  char extension[MAX_USERID];
  char last_modified[MAX_USERID];
  time_t last_modified_timestamp;
  int size;
  pthread_mutex_t reader_config;
  int readers;
  pthread_mutex_t can_use;
};

struct client {
  int devices[MAX_SAME_USER];
  char userid[MAX_USERID];
  char path_user[PATH_MAX];
  struct file_info files[MAXFILES];
  int logged_in;
  pthread_mutex_t config_mtx;
};

int read_until_eos(int sock, char * buffer);
int read_n_from_socket(int n, int sock, char *buffer);
int write_str_to_socket(int sock, char * str);
int read_and_save_to_file(int sock, char * filename, int fsize);
int write_file_to_socket(int sock, char * filename, int fsize);

#endif /*DROPBOXUTIL_H*/
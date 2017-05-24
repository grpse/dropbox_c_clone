#include <limits.h>
#include <pthread.h>
#include "dropboxUtil.h"

struct file_info{
  char name[MAX_USERID];
  char extension[MAX_USERID];
  char last_modified[MAX_USERID];
  int size;
  pthread_mutex_t reader_config;
  int readers;
  pthread_mutex_t can_use;
};

struct client{
  int devices[MAX_SAME_USER];
  char userid[MAX_USERID];
  char path_user[PATH_MAX];
  struct file_info files[MAXFILES];
  int logged_in;
  pthread_mutex_t config_mtx;
};

struct client_node{
  struct client * cli;
  struct client_node * next;
};

struct client_list{
  struct client_node * first_node;
};

void init_client_list(struct client_list * clist);
struct client * client_login(struct client_list * clist, char * userid, int device);
void client_logout(struct client * cli, int device);

void client_get_file_info(struct client * cli);

// Files
void file_init_read(struct file_info *file);
void file_end_read(struct file_info *file);
void file_init_write(struct file_info *file);
void file_end_write(struct file_info *file);

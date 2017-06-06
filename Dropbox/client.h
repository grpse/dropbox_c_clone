#include <limits.h>
#include <pthread.h>
#include "dropboxUtil.h"

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <limits.h>

#include "client.h"

void client_get_file_info(struct client * cli){
  DIR *d;
  struct dirent *f;
  d = opendir(cli->path_user);
  if(d){
    while((f = readdir(d)) != NULL){
      if (f->d_type == DT_REG){
        //printf("Arquivo: %s\n", f->d_name);
        struct file_info file;
        bzero(&file, sizeof(file));
        strcpy(file.name, f->d_name);
        char filename[PATH_MAX];
        struct stat attrib;
        sprintf(filename, "%s%s", cli->path_user, f->d_name);
        stat(filename, &attrib);
        file.size = attrib.st_size;
        strftime(file.last_modified, MAX_USERID, "%F %T", localtime(&attrib.st_mtime));
        // sprintf(file.last_modified, "%s", localtime(&attrib.st_mtime));
        char * ext = strchr(file.name, '.');
        if (ext!=NULL){
          strcpy(file.extension, ext+1);
        }


        //printf("%s %s %s %d\n", file.name, file.extension, file.last_modified, file.size);
        int i=0;
        while(cli->files[i].name[0] != '\0')
          i++;

        memcpy(&cli->files[i], &file, sizeof(file));
        printf("%s %s %s %d\n", cli->files[i].name, cli->files[i].extension, cli->files[i].last_modified, cli->files[i].size);
      }
    }
  }else{
    puts("Erro ao ler o diretorio");
    exit(0);
  }
}

void file_init_read(struct file_info *file){
  pthread_mutex_lock(&file->reader_config);
  file->readers++;
  // Primeiro leitor pega o lock de uso
  if (file->readers == 1)
    pthread_mutex_lock(&file->can_use);
  pthread_mutex_unlock(&file->reader_config);
}

void file_end_read(struct file_info *file){
  pthread_mutex_lock(&file->reader_config);
  file->readers--;
  // Primeiro leitor pega o lock de uso
  if (file->readers == 0)
    pthread_mutex_unlock(&file->can_use);
  pthread_mutex_unlock(&file->reader_config);
}

void file_init_write(struct file_info *file){
  pthread_mutex_lock(&file->can_use);
}

void file_end_write(struct file_info *file){
  pthread_mutex_unlock(&file->can_use);
}

void init_client(struct client * cli){
  bzero(cli->devices, MAX_SAME_USER * sizeof(cli->devices[0]));
  bzero(cli->userid, MAX_USERID);
  cli->logged_in = 0;
  pthread_mutex_init(&cli->config_mtx, NULL);
  int i;
  for (i=0; i<MAXFILES; i++){
    cli->files[i].size = 0;
    bzero(cli->files[i].extension, MAX_USERID);
    bzero(cli->files[i].name, MAX_USERID);
    bzero(cli->files[i].last_modified, MAX_USERID);
    cli->files[i].readers = 0;
    pthread_mutex_init(&cli->files[i].reader_config, NULL);
    pthread_mutex_init(&cli->files[i].can_use, NULL);
  }
}

void init_client_list(struct client_list * clist){
  clist->first_node = NULL;
}

struct client * client_login(struct client_list * clist, char * userid, int device){
  struct client_node * cnode = clist->first_node;
  struct client_node * prev = NULL;

  // if (cnode == NULL){
  //   clist->first_node = (struct client_node *)malloc(sizeof(struct client_node));
  //   clist->first_node->next = NULL;
  //   clist->first_node->cli = (struct client *)malloc(sizeof(struct client));
  //   init_client(clist->first_node->cli);
  //   return clist->first_node->cli;
  // }

  while (cnode != NULL){
    if (strcmp(cnode->cli->userid, userid) == 0){
      // Encontrou, coloca device
      int i;
      for (i=0; i<MAX_SAME_USER; i++){
        if (cnode->cli->devices[i] == 0)
          break;
      }
      if (i < MAX_SAME_USER){
        cnode->cli->devices[i] = device;
        return cnode->cli;
      }
      else{
        return NULL;
      }
    }
    prev = cnode;
    cnode = cnode->next;
  }

  // Por fim
  struct client_node * ncnode = (struct client_node *)malloc(sizeof(struct client_node));
  ncnode->next = NULL;
  ncnode->cli = (struct client *)malloc(sizeof(struct client));
  init_client(ncnode->cli);
  strcpy(ncnode->cli->userid, userid);
  ncnode->cli->devices[0] = device;
  if (prev != NULL)
    prev->next = ncnode;
  else
    clist->first_node = ncnode;

  return ncnode->cli;
}

void client_logout(struct client * cli, int device){
  int i;
  for(i=0; i<MAX_SAME_USER; i++){
    if (cli->devices[i] == device)
      cli->devices[i] = 0;
  }
}

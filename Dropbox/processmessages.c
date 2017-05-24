#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <limits.h>

#include "packager.h"
#include "dropboxUtil.h"
//#include "userlist.h"
#include "client.h"

#include "processmessages.h"

// Toda a vez que users for utilizado dentro do processamento das requisições
// do cliente, precisa mutex
pthread_mutex_t users_mutex;
//struct userlst_t users;
char dir_base[PATH_MAX];

struct client_list clients;

//const char * dir_base_users_server = "server-users/";

void init_users(){
  //userlst_create(&users, MAX_SAME_USER);
  pthread_mutex_init(&users_mutex, NULL);
  init_client_list(&clients);
  if(getcwd(dir_base, PATH_MAX) == NULL){
    puts("Erro ao abrir pasta!");
    exit(0);
  }
  strcat(dir_base, "/server-users/");
  struct stat st = {0};
  if (stat(dir_base, &st) == -1) {
    mkdir(dir_base, 0700);
    printf("directory created %s\n", dir_base);
  }
}

struct client * process_hi(char * user, int clsock){
  pthread_mutex_lock(&users_mutex);
  //int nconn = userlst_insert(&users, user);
  struct client * cl = client_login(&clients, user, clsock);
  pthread_mutex_unlock(&users_mutex);
  char str[BUF_SIZE];
  bzero(str, BUF_SIZE);
  if (cl == NULL){
    printf("Maximum connections reached, socket %d closed!\n", clsock);
    package_response(-1, "Maximum reached", str);
    write_str_to_socket(clsock, str);
  }else{
    package_response(1, "Success", str);
    write_str_to_socket(clsock, str);
    printf("The user %s is logged in.\n", cl->userid);
    struct stat st = {0};
    // Usar mutex ou var cond. para proteger
    sprintf(cl->path_user, "%s%s/", dir_base, cl->userid);
    if (stat(cl->path_user, &st) == -1) {
      mkdir(cl->path_user, 0700);
      printf("directory created for %s.\n", cl->userid);
    } else if (!cl->logged_in){
      // O arquivo já existe, vamos carregar as infos dele
      pthread_mutex_lock(&cl->config_mtx);
      client_get_file_info(cl);
      pthread_mutex_unlock(&cl->config_mtx);
    }
    cl->logged_in = 1;
  }
  return cl;
}

void process_ls(struct client * cli, int sock){
  char message[(MAX_USERID*3 + 20) * MAXFILES];
  char buffer[(MAX_USERID*3 + 20) * MAXFILES + 5];
  bzero(message, sizeof(message));
  int i;
  for(i=0;i<MAXFILES;i++){
    char line[MAX_USERID*3 + 20];
    bzero(line, sizeof(line));
    if(cli->files[i].name[0] != '\0'){
      // Tem informação
      sprintf(line, "\"%s\" %s %d\n", cli->files[i].name, cli->files[i].last_modified, cli->files[i].size);
      strcat(message, line);
    }
  }
  package_list(message, buffer);
  write_str_to_socket(sock, buffer);
}


// A princípio não terminada nem utilizada
void process_upd(char * message, struct client * cli, int sock){
  char * init_filename = strchr(message, '\"') + 1;
  char * end_filename = strchr(init_filename, '\"');
  *(end_filename++) = '\0';
  char * mtime = end_filename + 1;
  //puts(init_filename);
  //puts(mtime);
  int i;
  char send_buf[512];
  for(i=0; i<MAXFILES; i++){
    if (strcmp(cli->files[i].name, init_filename) == 0){
      if (strcmp(cli->files[i].last_modified, mtime) == 0){
        package_response(3,"Updated", send_buf);
        write_str_to_socket(sock, send_buf);
      }
      else{
        package_response(2,"Diff", send_buf);
        write_str_to_socket(sock, send_buf);

        // Envia novo arquivo
        package_file(cli->files[i].name, cli->files[i].last_modified, cli->files[i].size, send_buf);
        write_str_to_socket(sock, send_buf);

        char filename[PATH_MAX];
        sprintf(filename, "%s%s", cli->path_user, cli->files[i].name);

        int f = open(filename, O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if(f < 0){
          break;
        }
        int k = 0, r;
        char c;
        while(k < cli->files[i].size){
          r = read(f, &c, 1);
          if (r<0)
            break;
          r = write(sock, &c, 1);
          if (r<0)
            break;
          k += r;
          printf("%c", c);
        }
      }

      break;
    }
  }
  if (i==MAXFILES){
    package_response(1,"Not exist", send_buf);
    write_str_to_socket(sock, send_buf);
  }
}



void process_get(char * message, struct client * cli, int sock){
  char * init_filename = strchr(message, '\"');

  if (init_filename == NULL)
    return;

  init_filename++;

  char * end_filename = strchr(init_filename, '\"');

  if (end_filename == NULL)
    return;

  *(end_filename) = '\0';

  //char * mtime = end_filename + 1;
  //puts(init_filename);
  //puts(mtime);
  int i;
  char send_buf[512];
  for(i=0; i<MAXFILES; i++){
    if (strcmp(cli->files[i].name, init_filename) == 0){
      package_response(2,"Exist", send_buf);
      write_str_to_socket(sock, send_buf);

      // Envia novo arquivo
      package_file(cli->files[i].name, cli->files[i].last_modified, cli->files[i].size, send_buf);
      write_str_to_socket(sock, send_buf);

      char filename[PATH_MAX];
      sprintf(filename, "%s%s", cli->path_user, cli->files[i].name);
      write_file_to_socket(sock, filename, cli->files[i].size);
      break;
    }
  }
  if (i==MAXFILES){
    package_response(-1,"Not exist", send_buf);
    write_str_to_socket(sock, send_buf);
  }
}

void * client_process(void * clsock_ptr){
	int clsock = *((int *)clsock_ptr);
  //char path_user[PATH_MAX];
  //bzero(path_user, PATH_MAX);
  struct client * cli = NULL;

	free(clsock_ptr);

  int logged = 0;

	printf("Entered socket %d\n", clsock);

	char str[BUF_SIZE];

	int finish = 0;
	while(!finish){
		bzero(str, BUF_SIZE);
		int r = read_until_eos(clsock, str);
		if ((r < 0) || (str[0] == 0)){
			break;
		}

		printf("Command to socket %d: %s\n", clsock, str);
		// Processa comando
    // Sempre tem espaço, vide packager.c
		char * espaco = strchr(str, ' ');
		if (espaco != NULL){
			*(espaco++) = '\0';
			// puts(str);
			if ((strcmp("HI", str) == 0) && cli == NULL){
        if (espaco[0] != '\0'){
  				printf("User %s login in socket %d\n", espaco, clsock);
          cli = process_hi(espaco, clsock);
        }
			}else if (cli != NULL){
        // Chega nesse ponto somente quando já está logado
        if(strcmp(MES_LS, str) == 0){
          process_ls(cli, clsock);
        }
        else if (strcmp(MES_UPDATED, str) == 0){
          process_upd(espaco, cli, clsock);
        }
        else if (strcmp(MES_GET, str) == 0){
          process_get(espaco, cli, clsock);
        }
      }
		}
	}

	if (cli != NULL){
    pthread_mutex_lock(&users_mutex);
		//userlst_remove(&users, username);
    client_logout(cli, clsock);
    pthread_mutex_unlock(&users_mutex);
    printf("Saindo %s do socket %d...\n", cli->userid, clsock);
  }
	close(clsock);
	pthread_exit(NULL);
}

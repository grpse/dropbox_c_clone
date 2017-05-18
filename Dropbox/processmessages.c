#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
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
      client_get_file_info(cl);
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
      sprintf(line, "%s %s %s %d\n", cli->files[i].name, cli->files[i].extension, cli->files[i].last_modified, cli->files[i].size);
      strcat(message, line);
    }
  }
  package_list(message, buffer);
  write_str_to_socket(sock, buffer);
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
        if(strcmp("LS", str) == 0){
          process_ls(cli, clsock);
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

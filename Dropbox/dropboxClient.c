#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <limits.h>
#include <pwd.h>

#include "packager.h"
#include "dropboxUtil.h"

char buffer_read[(MAX_USERID*3 + 20) * MAXFILES + 5];
char buffer_write[2048];

int login(char * username, int sock){
  char message[2048];
  package_hi(username, message);
  int n = write_str_to_socket(sock, message);
  if (n < 0){
    puts("Erro no login..");
    return -1;
  }

  n = read_until_eos(sock, message);
  if (n > 0){
    // puts(message);
    char * res_val = strchr(message, ' ');
    *(res_val++) = '\0';
    char * res_str = strchr(res_val, ' ');
    *(res_str++) = '\0';
    puts(res_str);
    int res = atoi(res_val);
    if (res < 0){
      return -1;
    }
  }
  return n;
}

const char * base_dir = "~/";
int get_sync_dir(char * username){
  struct passwd *pw = getpwuid(getuid());
  char path_user[PATH_MAX];
  sprintf(path_user, "%s/sync_dir_%s", pw->pw_dir, username);
  struct stat st = {0};
  if (stat(path_user, &st) == -1) {
    mkdir(path_user, 0700);
    printf("directory created %s\n", path_user);
  }
  return 0;
}

int sockfd;
int connect_server(char * host, int port){
  struct sockaddr_in serv_addr;
  struct hostent *server;
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
    printf("ERROR opening socket\n");
    exit(1);
  }

  server = gethostbyname(host);
  if (server == NULL) {
    fprintf(stderr,"ERROR, no such host\n");
    exit(0);
  }

  serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(PORT);
	serv_addr.sin_addr = *((struct in_addr *)server->h_addr);
	bzero(&(serv_addr.sin_zero), 8);

  return connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr));
}

void list_files(int sock){
  int n;

  package_ls(buffer_write);

  n = write_str_to_socket(sock, buffer_write);
  if (n<0){
    puts("Error on LS call! Exiting...");
    exit(1);
  }

  n = read_until_eos(sock, buffer_read);
  if(n<0){
    puts("Error reading");
  }else{
    puts(buffer_read);
  }
}

int main(int argc, char *argv[])
{
  int n;
  struct sockaddr_in serv_addr;
  struct hostent *server;

  char buffer[256];
  char message[2048];
  char buffer_read[(MAX_USERID*3 + 20) * MAXFILES + 5];
  if (argc < 2) {
    fprintf(stderr,"usage %s username\n", argv[0]);
    exit(0);
  }


	if (connect_server("localhost", PORT) < 0){
    printf("ERROR connecting\n");
    exit(0);
  }

  n = login(argv[1], sockfd);

  if (n < 0){
    puts("Saindo...");
    exit(1);
  }
  get_sync_dir(argv[1]);
  puts("Arquivos no diretorio:");
  list_files(sockfd);

  while(1){
    printf("> ");
    bzero(buffer, 255);
    fgets(buffer, 255, stdin);
    strtok(buffer, "\r\n");
    //puts(buffer);

    //package_get(buffer, message);

    n = write_str_to_socket(sockfd, buffer);
    if (n < 0)
      printf("ERROR writing to socket\n");

    if(buffer[0] == 'L' && buffer[1] == 'S' && buffer[2] == ' '){
      n = read_until_eos(sockfd, buffer_read);
      if(n<0){
        puts("Erro no read");
      }else{
        puts(buffer_read);
      }
    }

  }
	close(sockfd);
  return 0;
}

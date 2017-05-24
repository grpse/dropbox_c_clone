#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <limits.h>
#include <pwd.h>

#define __USE_XOPEN
#include <time.h>
#include <utime.h>

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

// Entra com o ponteiro sem a mensagem
// Mensagem: *"nome" datahoramodif tamanho
char * get_file_info(char * buffer, char ** fname, char ** mtime, int * fsize){
  *fname = strchr(buffer, '\"');
  if (*fname == NULL)
    return NULL;

  (*fname) ++;

  *mtime = strchr(*fname, '\"');
  if (*mtime == NULL)
    return NULL;

  (*mtime) += 2;

  *((*mtime) - 2) = '\0';

  char * fsize_str = strchr(*mtime, ' ');
  if (fsize_str == NULL)
    return NULL;
  fsize_str = strchr(fsize_str + 1, ' ');
  if (fsize_str == NULL)
    return NULL;
  *(fsize_str++) = '\0';

  char * end;
  *fsize = strtol(fsize_str, &end, 10);
  if (*fsize == 0 && end == fsize_str)
    return NULL;

  return end;
}

// Mensagem: "RES valor str"
char * response_unpack(char * buffer, int * val, char ** message){
  char * valstr = strchr(buffer, ' ');
  if (valstr == NULL)
    return NULL;
  valstr++;

  char * mesinit = strchr(valstr, ' ');
  if (mesinit == NULL)
    return NULL;
  *mesinit = '\0';
  *message = mesinit + 1;

  char * end;
  *val = strtol(valstr, &end, 10);
  if (*val == 0 && end == valstr)
    return NULL;

  return (mesinit + strlen(*message) + 1);
}

const char * base_dir = "~/";
int get_sync_dir(char * username, int sock){
  struct passwd *pw = getpwuid(getuid());
  int n;
  char path_user[PATH_MAX], filename[PATH_MAX], time_modif[MAX_USERID];
  sprintf(path_user, "%s/sync_dir_%s", pw->pw_dir, username);
  struct stat st = {0};
  if (stat(path_user, &st) == -1) {
    mkdir(path_user, 0700);
    printf("directory created %s\n", path_user);
  }

  // Sincroniza o diretório
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
    char *init = strchr(buffer_read, ' ');
    while(init != NULL){
      init++;
      //puts(init);
      // ---- Temos as infos dos arquivos
      char * fname;
      char * mtime;
      int fsize;
      init = get_file_info(init, &fname, &mtime, &fsize);
      if (init != NULL){
        // Leu info do arquivo corretamente
        //printf("%s - %s - %d\n", fname, mtime, fsize);
        sprintf(filename, "%s/%s", path_user, fname);
        struct stat attrib;
        int ret;
        ret = stat(filename, &attrib);
        strftime(time_modif, MAX_USERID, "%F %T", localtime(&attrib.st_mtime));
        if ((ret == -1) || (strcmp(time_modif, mtime) != 0)){
          // Arquivo não existe no diretório ou é diferente
          // printf("File %s not exist or different.\n", fname);
          // Pede arquivo que é diferente
          package_get(fname, buffer_write);
          n = write_str_to_socket(sock, buffer_write);
          if (n<0)
            return -1;

          n = read_until_eos(sock, buffer_read);
          if (n<0)
            return -1;

          int rval;
          char * rvalstr;
          if(response_unpack(buffer_read, &rval, &rvalstr) == NULL)
            return -1;

          //printf("%d %s\n", rval, rvalstr);
          if (rval >= 0){
            // Arquivo virá
            n = read_until_eos(sock, buffer_read);
            if(get_file_info(buffer_read, &fname, &mtime, &fsize) == NULL)
              return -1;

            if (ret != -1){
              if(remove(filename) < 0)
                return -1;
            }
            // puts(filename);

            if(read_and_save_to_file(sock, filename, fsize) < 0)
              return -1;

            // Ajusta a hora de modificação
            struct utimbuf ntime;
            struct tm modtime;
            bzero(&modtime, sizeof(modtime));
            strptime(mtime, "%F %T", &modtime);
            time_t modif_time = mktime(&modtime);
            ntime.actime = modif_time;
            ntime.modtime = modif_time;
            if (utime(filename, &ntime) < 0)
              return -1;
          }
          else
            return -1;
        }
      }
    }
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
    char *init = strchr(buffer_read, ' ');
    if(init != NULL){
      init++;
      puts(init);
    }
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
    puts("Exiting...");
    exit(1);
  }
  if (get_sync_dir(argv[1], sockfd) < 0){
    puts("Impossible synchronize folders! Exiting...");
    exit(1);
  }

  char * ptr;

  while(1){
    printf("> ");
    bzero(buffer, 255);
    ptr = fgets(buffer, 255, stdin);
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

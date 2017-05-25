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

int sock;
int connect_server(char * host, int port){
  struct sockaddr_in serv_addr;
  struct hostent *server;
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1){
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

  return connect(sock,(struct sockaddr *) &serv_addr,sizeof(serv_addr));
}


int get_file(char * filename){
  // Faz get, recebe resposta e segue o baile
  int n;
  package_get(filename, buffer_write);
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
    char * fname;
    char * mtime;
    int fsize;
    n = read_until_eos(sock, buffer_read);
    if(get_file_info(buffer_read, &fname, &mtime, &fsize) == NULL)
      return -1;

    remove(filename);

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
  return 1;
}

const char * base_dir = "~/";
int sync_client(char * username){
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
          // Salva diretorio corrente
          getcwd(filename, sizeof(filename));
          // Troca pro diretorio path_user
          chdir(path_user);
          n = get_file(fname);
          // Retorna diretorio
          chdir(filename);
          if (n < 0)
            return -1;

          // package_get(fname, buffer_write);
          // n = write_str_to_socket(sock, buffer_write);
          // if (n<0)
          //   return -1;
          //
          // n = read_until_eos(sock, buffer_read);
          // if (n<0)
          //   return -1;
          //
          // int rval;
          // char * rvalstr;
          // if(response_unpack(buffer_read, &rval, &rvalstr) == NULL)
          //   return -1;
          //
          // //printf("%d %s\n", rval, rvalstr);
          // if (rval >= 0){
          //   // Arquivo virá
          //   n = read_until_eos(sock, buffer_read);
          //   if(get_file_info(buffer_read, &fname, &mtime, &fsize) == NULL)
          //     return -1;
          //
          //   if (ret != -1){
          //     if(remove(filename) < 0)
          //       return -1;
          //   }
          //   // puts(filename);
          //
          //   if(read_and_save_to_file(sock, filename, fsize) < 0)
          //     return -1;
          //
          //   // Ajusta a hora de modificação
          //   struct utimbuf ntime;
          //   struct tm modtime;
          //   bzero(&modtime, sizeof(modtime));
          //   strptime(mtime, "%F %T", &modtime);
          //   time_t modif_time = mktime(&modtime);
          //   ntime.actime = modif_time;
          //   ntime.modtime = modif_time;
          //   if (utime(filename, &ntime) < 0)
          //     return -1;
          // }
          // else
          //   return -1;
        }
      }
    }
  }


  return 0;
}

void list_files(){
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
    printf("%-44s %20s %12s\n", "---Filename---", "-----Mod. Time-----", "---Size---");
    while(init != NULL){
      init++;
      // puts(init);
      char * fname;
      char * mtime;
      int fsize;
      init = get_file_info(init, &fname, &mtime, &fsize);
      if (init != NULL){
        printf("%-44s %20s %12d\n", fname, mtime, fsize);
      }
    }
  }
}

int send_file(char * filename){
  int n;
  char time_modif[MAX_USERID];
  struct stat attr;
  if (stat(filename, &attr) < 0)
    return -1;

  strftime(time_modif, MAX_USERID, "%F %T", localtime(&attr.st_mtime));

  char * fname = strrchr(filename, '/');
  fname = fname == NULL ? filename : fname + 1;

  package_upload(fname, buffer_write);
  write_str_to_socket(sock, buffer_write);

  // Espera por resposta
  n = read_until_eos(sock, buffer_read);
  if (n < 0)
    return -1;

  int res_val;
  char * res_str;
  if (response_unpack(buffer_read, &res_val, &res_str) == NULL)
    return -1;

  if (res_val == 1){
    // Permitido envio de arquivo
    puts(res_str);

    package_file(fname, time_modif, attr.st_size, buffer_write);
    write_str_to_socket(sock, buffer_write);

    write_file_to_socket(sock, filename, attr.st_size);

    n = read_until_eos(sock, buffer_read);
    if (n < 0 || response_unpack(buffer_read, &res_val, &res_str) == NULL)
      return -1;

    printf("File sended. Status received: %s\n", res_str);
  }
}

int delete_file(char * filename){
  // Faz get, recebe resposta e segue o baile
  int n;
  package_delete(filename, buffer_write);
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
  puts(rvalstr);
  return 0;
}

int close_connection(){
  puts("Exiting...");
  package_close(buffer_write);
  write_str_to_socket(sock, buffer_write);
  if (read_until_eos(sock, buffer_read) < 0)
    puts("Erro saindo");
  else{
    int res;
    char *mes;
    response_unpack(buffer_read, &res, &mes);
    puts(mes);
    if (res == 1)
      return 1;
  }
  return -1;
}

int main(int argc, char *argv[])
{
  int n;
  struct sockaddr_in serv_addr;
  struct hostent *server;

  char buffer_console[2048];

  if (argc < 2) {
    fprintf(stderr,"usage %s username\n", argv[0]);
    exit(0);
  }


	if (connect_server("localhost", PORT) < 0){
    printf("ERROR connecting\n");
    exit(0);
  }

  n = login(argv[1], sock);

  if (n < 0){
    puts("Exiting...");
    exit(1);
  }
  if (sync_client(argv[1]) < 0){
    puts("Impossible synchronize folders! Exiting...");
    exit(1);
  }

  char * ptr;

  while(1){
    printf("> ");
    // bzero(buffer, 255);
    ptr = fgets(buffer_console, sizeof(buffer_console), stdin);
    strtok(buffer_console, "\r\n");

    char * f_esp = strchr(buffer_console, ' ');
    if (f_esp != NULL){
      *(f_esp++) = '\0';
      if (strcmp("upload", buffer_console) == 0){
        // Envia arquivo
        if(send_file(f_esp) < 0)
          puts("Error sending file.");
      }
      else if (strcmp("download", buffer_console) == 0){
        // Recebe arquivo
        if(get_file(f_esp) < 0)
          puts("Error downloading file.");
        else
          puts("Success on download file.");
      }
      else if (strcmp("delete", buffer_console) == 0){
        // Deleta arquivo
        if(delete_file(f_esp) < 0)
          puts("Error deleting file.");

        sync_client(argv[1]);
      }
    }
    if (strcmp("list", buffer_console) == 0){
      // Lista arquivos
      list_files();
    }
    else if (strcmp("get_sync_dir", buffer_console) == 0){
      // Sincroniza diretórios
      if (sync_client(argv[1]) >= 0)
        puts("Directories are synchronized.");
      else
        puts("Error synchronizing directories.");
    }
    else if (strcmp("exit", buffer_console) == 0){
      if (close_connection() >= 0)
        break;
    }
  }
	close(sock);
  return 0;
}

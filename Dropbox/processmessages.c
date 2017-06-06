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

#define __USE_XOPEN
#include <time.h>
#include <utime.h>

#include "packager.h"
#include "dropboxUtil.h"
#include "client.h"

#include "processmessages.h"

// Toda a vez que users for utilizado dentro do processamento das requisições
// do cliente, precisa mutex
pthread_mutex_t users_mutex;
//struct userlst_t users;
char dir_base[PATH_MAX];

struct client_list clients;

//const char * dir_base_users_server = "server-users/";

void init_users();
struct client *process_hi(char *user, int clsock);
void process_ls(struct client *cli, int sock);
void process_get(char *message, struct client *cli, int sock);
void process_upload(char *message, struct client *cli, int sock);
void process_delete(char *message, struct client *cli, int sock);
void process_exist(char *message, struct client *cli, int sock);

void *client_process(void *clsock_ptr)
{
  int clsock = *((int *)clsock_ptr);
  struct client *cli = NULL;

  free(clsock_ptr);

  int logged = 0;

  printf("Entered socket %d\n", clsock);

  char str[BUF_SIZE];

  int finish = 0;
  while (!finish)
  {
    bzero(str, BUF_SIZE);
    int r = read_until_eos(clsock, str);
    if ((r < 0) || (str[0] == 0))
    {
      break;
    }

    printf("Command to socket %d: %s\n", clsock, str);
    // Processa comando
    // Sempre tem espaço, vide packager.c
    char *espaco = strchr(str, ' ');
    if (espaco != NULL)
    {
      *(espaco++) = '\0';
      // puts(str);
      if ((strcmp(MES_HI, str) == 0) && cli == NULL)
      {
        if (espaco[0] != '\0')
        {
          printf("User %s login in socket %d\n", espaco, clsock);
          cli = process_hi(espaco, clsock);
        }
      }
      else if (cli != NULL)
      {
        // Chega nesse ponto somente quando já está logado
        if (strcmp(MES_LS, str) == 0)
        {
          client_get_file_info(cli);
          process_ls(cli, clsock);
        }
        else if (strcmp(MES_GET, str) == 0)
        {
          process_get(espaco, cli, clsock);
        }
        else if (strcmp(MES_UPLOAD, str) == 0)
        {
          process_upload(espaco, cli, clsock);
        }
        else if (strcmp(MES_DELETE, str) == 0)
        {
          process_delete(espaco, cli, clsock);
        }
        else if (strcmp(MES_EXIST, str) == 0)
        {
          process_exist(espaco, cli, clsock);
        }
        else if (strcmp(MES_CLOSE, str) == 0)
        {
          package_response(1, "Closed connection.", str);
          write_str_to_socket(clsock, str);
          finish = 1;
        }
      }
    }
  }

  if (cli != NULL)
  {
    pthread_mutex_lock(&users_mutex);
    //userlst_remove(&users, username);
    client_logout(cli, clsock);
    pthread_mutex_unlock(&users_mutex);
    printf("Saindo %s do socket %d...\n", cli->userid, clsock);
  }
  close(clsock);
  pthread_exit(NULL);
}

void init_users()
{
  //userlst_create(&users, MAX_SAME_USER);
  pthread_mutex_init(&users_mutex, NULL);
  init_client_list(&clients);
  if (getcwd(dir_base, PATH_MAX) == NULL)
  {
    puts("Erro ao abrir pasta!");
    exit(0);
  }
  strcat(dir_base, "/server-users/");
  struct stat st = {0};
  if (stat(dir_base, &st) == -1)
  {
    mkdir(dir_base, 0700);
    printf("directory created %s\n", dir_base);
  }
}

struct client *process_hi(char *user, int clsock)
{
  pthread_mutex_lock(&users_mutex);
  //int nconn = userlst_insert(&users, user);
  struct client *cl = client_login(&clients, user, clsock);
  pthread_mutex_unlock(&users_mutex);
  char str[BUF_SIZE];
  bzero(str, BUF_SIZE);
  if (cl == NULL)
  {
    printf("Maximum connections reached, socket %d closed!\n", clsock);
    package_response(-1, "Maximum reached", str);
    write_str_to_socket(clsock, str);
  }
  else
  {
    package_response(1, "Success", str);
    write_str_to_socket(clsock, str);
    printf("The user %s is logged in.\n", cl->userid);
    struct stat st = {0};
    // Usar mutex ou var cond. para proteger
    sprintf(cl->path_user, "%s%s/", dir_base, cl->userid);
    if (stat(cl->path_user, &st) == -1)
    {
      mkdir(cl->path_user, 0700);
      printf("directory created for %s.\n", cl->userid);
    }
    else if (!cl->logged_in)
    {
      // O arquivo já existe, vamos carregar as infos dele
      pthread_mutex_lock(&cl->config_mtx);
      client_get_file_info(cl);
      pthread_mutex_unlock(&cl->config_mtx);
    }
    cl->logged_in = 1;
  }
  return cl;
}

void process_ls(struct client *cli, int sock)
{
  char message[(MAX_USERID * 3 + 20) * MAXFILES];
  char buffer[(MAX_USERID * 3 + 20) * MAXFILES + 5];
  bzero(message, sizeof(message));
  int i;
  for (i = 0; i < MAXFILES; i++)
  {
    char line[MAX_USERID * 3 + 20];
    bzero(line, sizeof(line));
    file_init_read(&cli->files[i]);
    if (cli->files[i].name[0] != '\0')
    {
      // Tem informação
      sprintf(line, "\"%s\" %s %d\n", cli->files[i].name, cli->files[i].last_modified, cli->files[i].size);
      strcat(message, line);
    }
    file_end_read(&cli->files[i]);
  }
  package_list(message, buffer);
  write_str_to_socket(sock, buffer);
}

void process_get(char *message, struct client *cli, int sock)
{
  char *init_filename = strchr(message, '\"');

  if (init_filename == NULL)
    return;

  init_filename++;

  char *end_filename = strchr(init_filename, '\"');

  if (end_filename == NULL)
    return;

  *(end_filename) = '\0';

  //char * mtime = end_filename + 1;
  //puts(init_filename);
  //puts(mtime);
  int i;
  char send_buf[512];
  for (i = 0; i < MAXFILES; i++)
  {
    file_init_read(&cli->files[i]);
    if (strcmp(cli->files[i].name, init_filename) == 0)
    {
      package_response(2, "Exist", send_buf);
      write_str_to_socket(sock, send_buf);

      // Envia novo arquivo
      package_file(cli->files[i].name, cli->files[i].last_modified, cli->files[i].size, send_buf);
      write_str_to_socket(sock, send_buf);

      char filename[PATH_MAX];
      sprintf(filename, "%s%s", cli->path_user, cli->files[i].name);
      write_file_to_socket(sock, filename, cli->files[i].size);
      file_end_read(&cli->files[i]);
      break;
    }
    file_end_read(&cli->files[i]);
  }
  if (i == MAXFILES)
  {
    package_response(-1, "Not exist", send_buf);
    write_str_to_socket(sock, send_buf);
  }
}

void process_upload(char *message, struct client *cli, int sock)
{
  // Retira informação da mensagem

  // Por enquanto, só aceita a mensagem e espera pelo arquivo
  char buffer[512];
  package_response(1, "Ready to receive.", buffer);
  write_str_to_socket(sock, buffer);

  // Espera por mensagem FILE
  int n;
  n = read_until_eos(sock, buffer);
  if (n < 0)
    return;

  char *fs_espace = strchr(buffer, ' ');
  if (fs_espace == NULL)
    return;

  *(fs_espace++) = '\0';
  if (strcmp(MES_FILE, buffer) != 0)
    return;

  char *fname;
  char *mtime;
  int fsize;
  if (get_file_info(fs_espace, &fname, &mtime, &fsize) == NULL)
    return;

  char filename[PATH_MAX];
  sprintf(filename, "%s%s", cli->path_user, fname);

  if (read_and_save_to_file(sock, filename, fsize) < 0)
  {
    package_response(-1, "Error saving file", buffer);
    write_str_to_socket(sock, buffer);
    return;
  }

  // Ajusta a hora de modificação
  struct utimbuf ntime;
  struct tm modtime;
  bzero(&modtime, sizeof(modtime));
  strptime(mtime, "%F %T", &modtime);
  time_t modif_time = mktime(&modtime);
  ntime.actime = modif_time;
  ntime.modtime = modif_time;
  if (utime(filename, &ntime) < 0)
  {
    package_response(-1, "Error saving file", buffer);
    write_str_to_socket(sock, buffer);
    return;
  }

  // Por fim, salva na estrutura
  // No fim, pois se algo der errado não salvará na estrutura de arquivos
  int i;
  int free_to_write;
  for (i = 0; i < MAXFILES; i++)
  {
    free_to_write = 0;
    file_init_read(&cli->files[i]);
    if (cli->files[i].name[0] == '\0')
      free_to_write = 1;
    file_end_read(&cli->files[i]);
    if (free_to_write)
    {
      file_init_write(&cli->files[i]);
      strcpy(cli->files[i].name, fname);
      char *ext = strrchr(fname, '.');
      if (ext != NULL)
        strcpy(cli->files[i].extension, ext + 1);
      strcpy(cli->files[i].last_modified, mtime);
      cli->files[i].size = fsize;
      file_end_write(&cli->files[i]);
      break;
    }
  }

  package_response(1, "Success saved file", buffer);
  write_str_to_socket(sock, buffer);
}

void process_delete(char *message, struct client *cli, int sock)
{
  char *init_filename = strchr(message, '\"');

  if (init_filename == NULL)
    return;
  init_filename++;
  char *end_filename = strchr(init_filename, '\"');

  if (end_filename == NULL)
    return;
  *(end_filename) = '\0';

  int i;
  int rem;
  char send_buf[512];
  for (i = 0; i < MAXFILES; i++)
  {
    file_init_read(&cli->files[i]);
    rem = strcmp(cli->files[i].name, init_filename) == 0;
    file_end_read(&cli->files[i]);
    if (rem)
    {
      char filename[PATH_MAX];
      sprintf(filename, "%s%s", cli->path_user, init_filename);
      if (remove(filename) < 0)
      {
        package_response(-1, "Error removing", send_buf);
        write_str_to_socket(sock, send_buf);
        return;
      }
      file_init_write(&cli->files[i]);
      bzero(cli->files[i].name, sizeof(cli->files[i].name));
      bzero(cli->files[i].extension, sizeof(cli->files[i].extension));
      bzero(cli->files[i].last_modified, sizeof(cli->files[i].last_modified));
      cli->files[i].size = 0;
      file_end_write(&cli->files[i]);

      package_response(1, "File deleted", send_buf);
      write_str_to_socket(sock, send_buf);
      break;
    }
  }
  if (i == MAXFILES)
  {
    package_response(-1, "Not exist", send_buf);
    write_str_to_socket(sock, send_buf);
  }
}

void process_exist(char *message, struct client *cli, int sock)
{

  char response[PATH_MAX];
  int file_index, exist = 0;
  char *file_name = message;

  // remove as aspas na mensagem "packed"
  file_name = ++file_name;                 // remove aspas frontal
  file_name[strlen(file_name) - 1] = '\0'; // remove aspas traseira
  // Busca o arquivo por nome na lista de arquivos do usuário...
  for (file_index = 0; file_index < MAXFILES; file_index++)
  {
    if (cli->files[file_index].name[0] != '\0')
    {
      if (strcmp(cli->files[file_index].name, file_name) == 0)
      {
        exist = 1;
        break;
      }
    }
  }

  // Se existe escreve "true", senão "false" no socket aberto
  if (exist)
    package_response(1, "true", response);
  else
    package_response(0, "false", response);

  write_str_to_socket(sock, response);
}
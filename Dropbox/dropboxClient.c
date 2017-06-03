#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <limits.h>
#include <pwd.h>

#define __USE_XOPEN
#include <time.h>
#include <utime.h>
#include <signal.h>
#include <unistd.h>

#include "packager.h"
#include "dropboxUtil.h"

// String Macros
#define UPLOAD "upload"
#define LIST "list"
#define DOWNLOAD "download"
#define GET_SYNC_DIR "get_sync_dir"
#define DELETE "delete"
#define EXIT "exit"
#define EXIST "exist"

// Global variables
int sock;
char *username_g;
const char *base_dir = "~/";
char buffer_read[(MAX_USERID * 3 + 20) * MAXFILES + 5];
char buffer_write[2048];

// Prototypes
int connect_server(char *host, int port);
int login(char *username);
int get_file(char *filename);
int sync_client(char *username);
void list_files();
int send_file(char *filename);
int delete_file(char *filename);
void finalize_thread_and_close_connection(int exit_code);
int close_connection();

// helpers
int is_list_command(char *command_buffer);
int is_delete_command(char *command_buffer);
int is_download_command(char *command_buffer);
int is_upload_command(char *command_buffer);
int is_get_sync_dir_command(char *command_buffer);
int is_exit_command(char *command_buffer);
void first_sync_local_files(char *user_path);
int exist_local_sync_dir();

int sync_set = 0;
int is_first_sync = 1;
pthread_t file_sync_thread;
pthread_mutex_t file_sync_mutex;
void start_sync_monitor();
void *file_sync_monitor(void *);

void get_sync_dir_local_path(char **out_user_path);


int main(int argc, char *argv[])
{

  char command_buffer[2048] = "";
  char *username = username_g = argv[1];
  char *hostname = argv[2];
  int port = atoi(argv[3]);

  if (argc < 4)
  {
    fprintf(stderr, "usage: %s <username> <host> <port>\n", argv[0]);
    exit(0);
  }

  if (connect_server(hostname, port) < 0)
  {
    printf("ERROR connecting\n");
    exit(0);
  }

  if (login(username) < 0)
  {
    puts("Error on login. Exiting...");
    exit(1);
  }

  // se já existe o sync_dir_<username>, inicia a sincronização
  if (exist_local_sync_dir()) {
    start_sync_monitor();
  }

  // registra um sinal para qualquer problema que ocorra para finalizar
  // corretamente a conexão com o servidor
  signal(SIGINT, finalize_thread_and_close_connection);
  signal(SIGUSR1, finalize_thread_and_close_connection);
  signal(SIGKILL, finalize_thread_and_close_connection);
  signal(SIGSTOP, finalize_thread_and_close_connection);

  char *ptr;
  char *f_esp;

  while (1)
  {
    printf("> ");
    ptr = fgets(command_buffer, sizeof(command_buffer), stdin);
    strtok(command_buffer, "\r\n");

    // Remove comandos com espaços colocando '\0' para finalizar a string
    f_esp = strchr(command_buffer, ' ');
    if (f_esp != NULL)
      *(f_esp++) = '\0';

    // Identifica comando utilizado e explicita caso haja algum erro
    if (is_upload_command(command_buffer))
    {
      if (send_file(f_esp) < 0)
        puts("Error sending file.");
    }
    else if (is_download_command(command_buffer))
    {
      if (get_file(f_esp) < 0)
        puts("Error downloading file.");
    }
    else if (is_delete_command(command_buffer))
    {
      if (delete_file(f_esp) < 0)
        puts("Error deleting file.");
      if (sync_set && sync_client(username) < 0)
        puts("Error synchronizing directories.");
    }
    else if (is_list_command(command_buffer))
    {
      list_files();
    }
    else if (is_get_sync_dir_command(command_buffer))
    {
      start_sync_monitor();
    }
    else if (is_exit_command(command_buffer))
    {
      // WARNING: Precisa ser o finalizador do main
      // para lidar com o sinais corretamente
      finalize_thread_and_close_connection(-1);
      break;
    }
  }

  return 0;
}

int connect_server(char *host, int port)
{
  struct sockaddr_in serv_addr;
  struct hostent *server;
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    fprintf(stderr, "ERROR opening socket\n");
    exit(1);
  }

  server = gethostbyname(host);
  if (server == NULL)
  {
    fprintf(stderr, "ERROR, no such host\n");
    exit(0);
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);
  serv_addr.sin_addr = *((struct in_addr *)server->h_addr);
  bzero(&(serv_addr.sin_zero), 8);

  return connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
}

int login(char *username)
{
  char message[2048];
  package_hi(username, message);
  int n = write_str_to_socket(sock, message);
  if (n < 0)
  {
    puts("Error on login..");
    return -1;
  }

  n = read_until_eos(sock, message);
  if (n > 0)
  {
    // puts(message);
    char *res_val = strchr(message, ' ');
    *(res_val++) = '\0';
    char *res_str = strchr(res_val, ' ');
    *(res_str++) = '\0';
    puts(res_str);
    int res = atoi(res_val);
    if (res < 0)
      return -1;
  }
  return n;
}

int get_file(char *filename)
{
  // Faz get, recebe resposta e segue o baile
  int n;
  package_get(filename, buffer_write);
  n = write_str_to_socket(sock, buffer_write);
  if (n < 0)
    return -1;

  n = read_until_eos(sock, buffer_read);
  if (n < 0)
    return -1;

  int rval;
  char *rvalstr;
  if (response_unpack(buffer_read, &rval, &rvalstr) == NULL)
    return -1;

  //printf("%d %s\n", rval, rvalstr);
  if (rval >= 0)
  {
    // Arquivo virá
    char *remote_filename;
    char *mtime;
    int fsize;
    n = read_until_eos(sock, buffer_read);
    if (get_file_info(buffer_read, &remote_filename, &mtime, &fsize) == NULL)
      return -1;

    remove(filename);

    if (read_and_save_to_file(sock, filename, fsize) < 0)
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

int sync_client(char *username)
{
  char user_path[PATH_MAX];

  // descobre o diretório sync_dir do usuário
  get_sync_dir_local_path(user_path);

  // Caso não exista o diretório sync_dir_<username>, cria-o
  if (!exist_local_sync_dir())
    mkdir(user_path, 0775);

  // Verifica primeiramente a existência dos arquivos
  // da pasta ~/sync_dir_<username> no servidor
  // OBS: Somente da primeira vez que faz a sincronização.
  if (is_first_sync)
  {
    first_sync_local_files(user_path);
    // not a first sync anymore
    is_first_sync = 0;
  }

  // Sincroniza o diretório
  package_ls(buffer_write);

  int error_on_write_to = write_str_to_socket(sock, buffer_write);
  if (error_on_write_to < 0)
  {
    puts("Error on LS call! Exiting...");
    exit(1);
  }

  int error_on_read = read_until_eos(sock, buffer_read);
  if (error_on_read < 0)
    puts("Error reading");
  else
  {
    char *next_file_info = strchr(buffer_read, ' ');
    while (next_file_info != NULL)
    {
      next_file_info++;
      // ---- Temos as infos dos arquivos
      char *remote_file_name;
      char *remote_file_last_time_modification;
      int fsize;
      next_file_info = get_file_info(next_file_info, &remote_file_name, &remote_file_last_time_modification, &fsize);
      if (next_file_info != NULL)
      {
        char filename[PATH_MAX];
        char time_modif[MAX_USERID];
        struct tm tm;
        struct stat local_file_attributes;
        char *DATE_FORMAT = "%Y-%m-%d %H:%M:%S";

        // Leu info do arquivo corretamente.
        // Pega os tempos da última modificação do arquivo
        sprintf(filename, "%s/%s", user_path, remote_file_name);
        int ret = stat(filename, &local_file_attributes);
        strftime(time_modif, MAX_USERID, DATE_FORMAT, localtime(&local_file_attributes.st_mtime));

        // Coleta os tempos de forma numérica (UNIX TIMESTAMP)
        strptime(remote_file_last_time_modification, DATE_FORMAT, &tm);
        time_t file_server_time = mktime(&tm);

        strptime(time_modif, DATE_FORMAT, &tm);
        time_t file_local_time = mktime(&tm);

        // verifica o atributo de última modificação...
        // Caso o arquivo não exista ou se o arquivo local for mais antigo que o do servidor, faz download do servidor...
        if (ret == -1 || file_local_time < file_server_time)
        {
          int n;
          // Salva o diretório corrente
          char *result_file_name = getcwd(filename, sizeof(filename));
          // Troca pro diretorio user_path
          n = chdir(user_path);
          // faz download do arquivo do servidor
          n = get_file(remote_file_name);
          if (n < 0)
            return -1;
          // Retorna diretorio para o local do executável
          n = chdir(filename);
        }
        // ... se o arquivo do servidor for mais antigo que o local, faz upload para o servidor...
        else if (file_server_time < file_local_time)
        {
          send_file(filename);
        }
      }
    }
  }

  return 0;
}

void list_files()
{
  int n;

  package_ls(buffer_write);

  n = write_str_to_socket(sock, buffer_write);
  if (n < 0)
  {
    puts("Error on LS call! Exiting...");
    exit(1);
  }

  n = read_until_eos(sock, buffer_read);
  if (n < 0)
  {
    puts("Error reading");
  }
  else
  {
    char *init = strchr(buffer_read, ' ');
    printf("%-44s %20s %12s\n", "---Filename---", "-----Mod. Time-----", "---Size---");
    while (init != NULL)
    {
      init++;
      // puts(init);
      char *remote_filename;
      char *mtime;
      int fsize;
      init = get_file_info(init, &remote_filename, &mtime, &fsize);
      if (init != NULL)
      {
        printf("%-44s %20s %12d\n", remote_filename, mtime, fsize);
      }
    }
  }
}

int send_file(char *filename)
{
  int n;
  char time_modif[MAX_USERID];
  struct stat attr;
  if (stat(filename, &attr) < 0)
    return -1;

  strftime(time_modif, MAX_USERID, "%F %T", localtime(&attr.st_mtime));

  char *remote_filename = strrchr(filename, '/');
  remote_filename = remote_filename == NULL ? filename : remote_filename + 1;

  package_upload(remote_filename, buffer_write);
  write_str_to_socket(sock, buffer_write);

  // Espera por resposta
  n = read_until_eos(sock, buffer_read);
  if (n < 0)
    return -1;

  int res_val;
  char *res_str;
  if (response_unpack(buffer_read, &res_val, &res_str) == NULL)
    return -1;

  if (res_val == 1)
  {
    // Permitido envio de arquivo
    package_file(remote_filename, time_modif, attr.st_size, buffer_write);
    write_str_to_socket(sock, buffer_write);

    write_file_to_socket(sock, filename, attr.st_size);

    n = read_until_eos(sock, buffer_read);
    if (n < 0 || response_unpack(buffer_read, &res_val, &res_str) == NULL)
      return -1;
  }
}

int delete_file(char *filename)
{
  // Faz get, recebe resposta e segue o baile
  int n;
  package_delete(filename, buffer_write);
  n = write_str_to_socket(sock, buffer_write);
  if (n < 0)
    return -1;

  n = read_until_eos(sock, buffer_read);
  if (n < 0)
    return -1;

  int rval;
  char *rvalstr;
  if (response_unpack(buffer_read, &rval, &rvalstr) == NULL)
    return -1;
  return 0;
}

/**
Finaliza os recursos e sai do programa corretamente
tanto para responder ao comando de "exit" como para
manipular um sinal recebido, como CTRL+C.
*/
void finalize_thread_and_close_connection(int exit_code)
{
    // sinaliza fechar a conexão com o servidor
    if (close_connection() < 0)
      puts("Error closing connection on server.");
    // Se temos um diretório sincronizada pelo inotify e uma thread
    // cancelamos a execução da thread.
    if (sync_set)
      pthread_cancel(file_sync_thread);
    // fecha o socket
    close(sock);
    // fecha o programa
    exit(0);
}

int close_connection()
{
  puts("\rExiting...");
  package_close(buffer_write);
  write_str_to_socket(sock, buffer_write);
  if (read_until_eos(sock, buffer_read) < 0)
    puts("Error exiting...");
  else
  {
    int res;
    char *mes;
    response_unpack(buffer_read, &res, &mes);
    puts(mes);
    if (res == 1)
      return 1;
  }
  return -1;
}

int is_list_command(char *command_buffer)
{
  return strcmp(LIST, command_buffer) == 0;
}

int is_delete_command(char *command_buffer)
{
  return strcmp(DELETE, command_buffer) == 0;
}

int is_download_command(char *command_buffer)
{
  return strcmp(DOWNLOAD, command_buffer) == 0;
}

int is_upload_command(char *command_buffer)
{
  return strcmp(UPLOAD, command_buffer) == 0;
}

int is_get_sync_dir_command(char *command_buffer)
{
  return strcmp(GET_SYNC_DIR, command_buffer) == 0;
}

int is_exit_command(char *command_buffer)
{
  return strcmp(EXIT, command_buffer) == 0;
}

void first_sync_local_files(char *user_path)
{
  DIR *dir;
  struct dirent *ent;
  char file_name_send_buffer[PATH_MAX];
  char response_buffer[10];
  char file_names[MAXFILES][PATH_MAX];
  char file_name_with_path[PATH_MAX];

  // Lista os arquivos do diretório "sync_dir_<username>" local do usuário
  if ((dir = opendir(user_path)) != NULL)
  {
    // Lê cada entrada
    while ((ent = readdir(dir)) != NULL)
    {
      // Copia o nome do arquivo para o array de nomes de arquivos
      // se não for '.' ou '..'
      if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0)
      {
        // Verifica se o arquivo existe no servidor
        char *file_name = ent->d_name;
        package_exist(file_name, file_name_send_buffer);

        // envia o comando "exist" ...
        int error_on_write_to_socket = write_str_to_socket(sock, file_name_send_buffer);
        if (error_on_write_to_socket < 0)
        {
          perror("Error on writing to socket");
          exit(1);
        }
        // recebe a resposta do servidor "true" ou "false"
        int error_on_read_from_socket = read_until_eos(sock, response_buffer);
        if (error_on_read_from_socket < 0)
        {
          perror("Error on reading from socket");
          exit(1);
        }
        
        // "unpack" a resposta do servidor
        int response_status;
        char* unpacked_response;
        char* unpack_status = response_unpack(response_buffer, &response_status, &unpacked_response);
        
        // Se o arquivo não existe, concatena com ~/sync_dir_<username> e envia o arquivo
        if (strcmp(unpacked_response, "false") == 0)
        {
          sprintf(file_name_with_path, "%s/%s", user_path, file_name);
          send_file(file_name_with_path);
        }
      }
    }
    closedir(dir);
  }
  else
  {
    /* could not open directory */
    perror("Not sync yet");
  }
}

int exist_local_sync_dir()
{
  char user_path[PATH_MAX];
  struct stat st = {0};
  get_sync_dir_local_path(user_path);
  return stat(user_path, &st) != -1;
}

void start_sync_monitor()
{
  sync_set = 1;
  // Sincronização inicial
  sync_client(username_g);
  pthread_create(&file_sync_thread, NULL, file_sync_monitor, NULL);
}

void *file_sync_monitor(void *param)
{
  {
#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (1024 * (EVENT_SIZE + 16))
  }

  struct passwd *pw = getpwuid(getuid());
  int n;
  char user_path[PATH_MAX], filename[PATH_MAX], time_modif[MAX_USERID];
  sprintf(user_path, "%s/sync_dir_%s", pw->pw_dir, username_g);

  int length, i = 0;
  int fd;
  int wd;
  char buffer[BUF_LEN];

  fd = inotify_init();

  if (fd < 0)
    perror("inotify_init");

  // unsigned int watchEvents = IN_MODIFY | IN_CREATE | IN_DELETE;
  wd = inotify_add_watch(fd, user_path, IN_ALL_EVENTS);
  while ((length = read(fd, buffer, BUF_LEN)) > 0)
  {

    i = 0;
    if (length < 0)
      perror("read");

    while (i < length)
    {
      struct inotify_event *event = (struct inotify_event *)&buffer[i];
      if (event->len)
      {
        // Cria os testes de possíveis eventos
        int created = event->mask & IN_CREATE;
        int deleted = event->mask & IN_DELETE;
        int modified = event->mask & IN_MODIFY;

        // Descobre o novo caminho do arquivo
        char filepath[PATH_MAX];
        sprintf(filepath, "%s/%s", user_path, event->name);

        // Se for criado novo arquivo envia ele, se for deletado, deleta no servidor, se for modificado, sincroniza com os arquivos do servidor
        SCOPELOCK(file_sync_mutex, {
          if (created)
            send_file(filepath);
          else if (deleted)
            delete_file(filepath);
          else if (modified)
            sync_client(username_g);
        });
      }
      unsigned int nextPace = EVENT_SIZE + event->len;
      i += nextPace;
    }

    memset(buffer, 0, BUF_LEN);
  }

  inotify_rm_watch(fd, wd);
  close(fd);

  return NULL;
}

// Recebe o diretório "sync_dir_<username>" local
// do usuário pelo parâmetros de saída
void get_sync_dir_local_path(char **out_user_path)
{
  struct passwd *pw = getpwuid(getuid());
  sprintf((char *)&*out_user_path, "%s/sync_dir_%s", pw->pw_dir, username_g);
}
#include "dropboxClient.h"

// Variáveis globais
int sock_g;
char *username_g;
char buffer_read[(MAX_USERID * 3 + 20) * MAXFILES + 5];
char buffer_write[2048];

// Variáveis para a sincronização de arquivos
int sync_set = 0;
int is_first_sync = 1;
pthread_t file_sync_thread;
pthread_mutex_t file_sync_mutex;
pthread_mutex_t try_sync_mutex;

int main(int argc, char *argv[])
{

  char command_buffer[2048] = "";
  username_g = argv[1];
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

  if (login(username_g) < 0)
  {
    puts("Error on login. Exiting...");
    exit(1);
  }

  // se já existe o sync_dir_<username>, inicia a sincronização
  if (exist_local_sync_dir())
  {
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
    // pega entrada do usuário
    printf("> ");
    ptr = fgets(command_buffer, sizeof(command_buffer), stdin);
    strtok(command_buffer, "\r\n");

    // Remove comandos com espaços colocando '\0' para finalizar a string
    f_esp = strchr(command_buffer, ' ');
    if (f_esp != NULL)
      *(f_esp++) = '\0';

    // Identifica comando utilizado e explicita caso haja algum erro
    SCOPELOCK(file_sync_mutex, {
    if (is_upload_command(command_buffer))
    {
      if (file_copy_to_sync_dir(f_esp, strrchr(f_esp, '/')) < 0)
        perror("Error sending file.");
    }
    else if (is_download_command(command_buffer))
    {
      if (get_file(f_esp) < 0)
        perror("Error downloading file.");
    }
    else if (is_delete_command(command_buffer))
    {
      if (delete_file(f_esp) < 0)
        perror("Error deleting file.");
      if (sync_set && sync_client(username_g) < 0)
        perror("Error synchronizing directories.");
    }
    else if (is_list_command(command_buffer))
    {
      if (list_files() < 0)
        perror("Error on list files");
    }
    else if (is_get_sync_dir_command(command_buffer))
    {
      // Reinicializa sincronização
      // Caso tenham mudanças no servidor feitas pelo mesmo cliente em outra máquina,
      // elas serão trazidas
      pthread_cancel(file_sync_thread);
      sync_set = 0;
      start_sync_monitor();
    }
    else if (is_exit_command(command_buffer))
    {
      // WARNING: Precisa ser o finalizador do main
      // para lidar com o sinais corretamente
      finalize_thread_and_close_connection(-1);
      break;
    }
    });
  }

  return 0;
}

/*
Conecta cliente ao servidor.
Recebe o host e a porta para fazer conexão.
retorna > 0 se a conexão ocorreu com sucesso.
*/
int connect_server(char *host, int port)
{
  struct sockaddr_in serv_addr;
  struct hostent *server;
  sock_g = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_g < 0)
    return -1;

  // traduz o hostname de string para uma
  // struct hostent*
  server = gethostbyname(host);
  if (server == NULL)
    return -1;
  // preenche a estrutura de sockaddr...
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);
  serv_addr.sin_addr = *((struct in_addr *)server->h_addr);
  bzero(&(serv_addr.sin_zero), 8);
  // ... para fazer chamar a função connect
  return connect(sock_g, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
}

/*
Faz o login do usuário no servidor
enviando o nome do usuário em um
pacote "HI".
Retorna 0 caso tenha sucesso e -1
caso tenha ocorrido algum erro.
*/
int login(char *username)
{
  // enpacota uma mensagem "HI" com o nome do usuário
  package_hi(username, buffer_write);
  // envia o comando para o servidor
  if (write_str_to_socket(sock_g, buffer_write) < 0)
    return -1;
  // recebe o retorno
  if (read_until_eos(sock_g, buffer_read) < 0)
    return -1;
  // desenpacota a resposta
  int response_status;
  char* response_value;
  if (response_unpack(buffer_read, &response_status, &response_value) == NULL)
    return -1;
  // imprime a resposta e verifica se o login foi feito de forma correta
  puts(response_value);
  if (response_status < 0)
    return -1;
  return 0;
}

/*
Faz download de um arquivo presente no servidor
para a pasta onde está a execução do programa.
Retorna 0 caso tenha sucesso e -1 caso tenha
ocorrido algum erro.
Usado buffer interno para recepção, pois o
ponteiro de filename está no buffer_read global,
o que, algumas vezes gerava erros na execução
*/
int get_file(char *filename)
{
  int response_status;
  char *rvalstr;
  char buffer_read_2[1024];
  // Enpacota o comando "get"
  package_get(filename, buffer_write);
  // envia o comando "get"
  if (write_str_to_socket(sock_g, buffer_write) < 0)
    return -1;
  // recebe a resposta do servidor
  if (read_until_eos(sock_g, buffer_read_2) < 0)
    return -1;
  // desenpacota o a resposta do servidor
  if (response_unpack(buffer_read_2, &response_status, &rvalstr) == NULL)
    return -1;
  // se permitido, obtém o arquivo
  if (response_status >= 0)
  {

    char *remote_filename;
    char *remote_file_last_modification;
    int fsize;
    // recebe os dados do arquivo
    if (read_until_eos(sock_g, buffer_read_2) < 0)
      return -1;
    // obtém os dados do arquivo
    if (get_file_info(buffer_read_2, &remote_filename, &remote_file_last_modification, &fsize) == NULL)
      return -1;
    // se houve algum arquivo com o mesmo nome no diretório corrente, remove-o
    remove(filename);

    // efetua o download do arquivo remoto para o diretório corrente
    if (read_and_save_to_file(sock_g, filename, fsize) < 0)
      return -1;

    // Ajusta a hora de modificação para a hora local
    struct utimbuf ntime;
    struct tm modtime = {0};
    // formata a data para "%Y-%m-%D %H:%M:%S"
    strptime(remote_file_last_modification, "%F %T", &modtime);
    // transforma em tempo numérico e preenche a estrutura
    time_t modif_time = mktime(&modtime);
    ntime.actime = modif_time;
    ntime.modtime = modif_time;
    // salva a nova data de modificação no arquivo feito o download
    if (utime(filename, &ntime) < 0)
      return -1;
  }

  return 0;
}

/*
Sincroniza os arquivos locais com os do servidor,
pela data da última modificação. A operação é
feita por arquivo. Caso um arquivo local seja
mais novo é feito o upload deste, caso mais
antigo ou não exista um equivalente local,
é feito o download deste.
Estas operações são realizadas no diretório
"~/sync_dir_<username>".
Retorna 0 caso sucesso e -1 caso algum erro
ocorra.
*/
int sync_client(char *username)
{
  char user_path[PATH_MAX];

  // Obtém o diretório "~/sync_dir_<username>"
  get_sync_dir_local_path(user_path);

  // Caso não exista o diretório sync_dir_<username>, cria-o
  if (!exist_local_sync_dir())
    mkdir(user_path, 0775);

  // Lista os arquivos remotos
  package_ls(buffer_write);
  // envia "list" para o servidor
  if (write_str_to_socket(sock_g, buffer_write) < 0)
    return -1;
  // recebe o buffer com a lista de arquivos presentes no servidor
  if (read_until_eos(sock_g, buffer_read) < 0)
    return -1;

  // escapa o cabeçalho da resposta do comando obtida do servidor
  char *next_file_info = strchr(buffer_read, ' ');
  while (next_file_info != NULL)
  {
    next_file_info++;

    // ---- Temos as infos dos arquivos
    char *remote_file_name;
    char *remote_file_last_time_modification;
    int remote_file_size;

    // recebe a informação de um arquivo da lista recebida
    next_file_info = get_file_info(next_file_info, &remote_file_name, &remote_file_last_time_modification, &remote_file_size);
    if (next_file_info != NULL)
    {
      char file_path[PATH_MAX];
      struct tm tm;
      struct stat local_file_attributes;
      char local_file_last_time_modification[MAX_USERID];
      char *DATE_FORMAT = "%Y-%m-%d %H:%M:%S";


      // Obtém os atributos do arquivo local
      sprintf(file_path, "%s/%s", user_path, remote_file_name);
      int ret = stat(file_path, &local_file_attributes);
      // Obtém as datas da última modificação do arquivo local
      // OBS: Faz uma conversão para string primeiro, pois
      //      se lermos diretamente a data da última modificação
      //      do arquivo local temos a informação em milisegundos, mas
      //      remotamente não possuímos, logo, trucamos para segundos.
      strftime(local_file_last_time_modification, MAX_USERID, DATE_FORMAT, localtime(&local_file_attributes.st_mtime));

      // Coleta as datas de última modificação de forma numérica (UNIX TIMESTAMP)
      strptime(remote_file_last_time_modification, DATE_FORMAT, &tm);
      time_t file_server_time = mktime(&tm);

      strptime(local_file_last_time_modification, DATE_FORMAT, &tm);
      time_t file_local_time = mktime(&tm);

      // verifica o atributo de última modificação...
      // Caso o arquivo não exista ou se o arquivo local for mais antigo que o do servidor, faz download do servidor...
      if (ret == -1 || file_local_time < file_server_time)
      {
        int n;
        // Salva o diretório corrente
        char *result_file_name = getcwd(file_path, sizeof(file_path));
        // Troca pro diretorio user_path
        n = chdir(user_path);
        if (n < 0){
        	puts("Erro chdir");
        	return -1;
        }
        // faz download do arquivo do servidor
        n = get_file(remote_file_name);
        if (n < 0){
          n = chdir(file_path);
          return -1;
        }
        // Retorna diretorio para o local do executável
        n = chdir(file_path);
      }
      // ... se o arquivo do servidor for mais antigo que o local, faz upload para o servidor...
      else if (file_server_time < file_local_time)
        send_file(file_path);
    }
  }

  return 0;
}

/*
Verifica se existe a pasta "sync_dir_<username>"
na máquina local. Retorna 1 se existe e outro
valor caso contrário.
*/
int exist_local_sync_dir()
{
  char user_path[PATH_MAX];
  struct stat st = {0};
  get_sync_dir_local_path(user_path);
  return stat(user_path, &st) != -1;
}

/*
Lista os arquivos presentes na pasta do usuário no servidor.
Retorna 0 para sucesso e -1 caso tenha ocorrido algum erro.
*/
int list_files()
{
  // enpacota o comando "list"
  package_ls(buffer_write);
  // envia o comando "list"
  if (write_str_to_socket(sock_g, buffer_write) < 0)
    return -1;
  // recebe a resposta do servidor
  if (read_until_eos(sock_g, buffer_read) < 0)
    return -1;
  // recebe o buffer com as informações dos arquivos do usuário no servidor
  char *file_info_buffer = strchr(buffer_read, ' ');
  // imprime cabeçalho de nome, data da última modificação e tamanho do arquivo
  printf("%-44s %20s %12s\n", "---Filename---", "-----Mod. Time-----", "---Size---");
  while (file_info_buffer != NULL)
  {
    // passa o ponteiro para o início do retorno do
    // servidor, logo após o cabeçalho da resposta
    file_info_buffer++;

    char *remote_file_name;
    char *remote_file_last_modified_date;
    int remote_file_size;
    // extrai as informações do arquivo e os imprime na tela
    file_info_buffer = get_file_info(file_info_buffer, &remote_file_name, &remote_file_last_modified_date, &remote_file_size);
    if (file_info_buffer != NULL)
      printf("%-44s %20s %12d\n", remote_file_name, remote_file_last_modified_date, remote_file_size);
  }

  return 0;
}

/*
Envia um arquivo local para o servidor.
Retorna 0 para sucesso.
Retorna -1 caso tenha ocorrido algum erro.
Usado buffer interno para recepção, pois o
ponteiro de filename está no buffer_read global,
o que, algumas vezes gerava erros na execução
*/
int send_file(char *file_path)
{
  int response_status;
  char *res_str;
  char time_modif[MAX_USERID];
  struct stat local_file_attributes;
  char buffer_read_2[1024];

  // Captura os atributos do arquivo local
  if (stat(file_path, &local_file_attributes) < 0)
    return -1;

  // Transforma a data da última modificação em string
  strftime(time_modif, MAX_USERID, "%F %T", localtime(&local_file_attributes.st_mtime));

  // Adiquire o nome do arquivo local
  char *local_file_name = strrchr(file_path, '/');
  local_file_name = local_file_name == NULL ? file_path : local_file_name + 1;

  // enpacota o commando de upload com o nome do arquivo a ser feito o upload
  package_upload(local_file_name, buffer_write);

  // envia o comando "upload"
  if (write_str_to_socket(sock_g, buffer_write) < 0)
    return -1;
  // recebe a resposta do comando "upload"
  if (read_until_eos(sock_g, buffer_read_2) < 0)
    return -1;
  // desenpacota a resposta ao comando
  if (response_unpack(buffer_read_2, &response_status, &res_str) == NULL)
    return -1;
  // se o servidor permitiu o envio do arquivo
  if (response_status == 1)
  {
    // enpacota o nome do arquivo
    package_file(local_file_name, time_modif, local_file_attributes.st_size, buffer_write);
    // envia o nome do arquivo, seu tamanho e data da última modificação para o servidor
    if (write_str_to_socket(sock_g, buffer_write) < 0)
      return -1;
    // envia o arquivo para o servidor
    if (write_file_to_socket(sock_g, file_path, local_file_attributes.st_size) < 0)
      return -1;
    // aguarda uma resposta
    if (read_until_eos(sock_g, buffer_read_2) < 0 ||
        response_unpack(buffer_read_2, &response_status, &res_str) == NULL)
      return -1;

    // Se foi feito o upload correto, copia para o ~/sync_dir_<username>
    if (sync_set){
      TRY_LOCK_SCOPE(try_sync_mutex, {
        file_copy_to_sync_dir(file_path, local_file_name);
      }, {
        file_copy_to_sync_dir(file_path, local_file_name);
      });
    }
  }
  else
    return -1;

  return 0;
}

/*
Deleta um arquivo do servidor identificado pelo parâmetro "filename".
Retorna 0 caso tenha sido deletado com sucesso e -1 caso tenha ocorrido
algum problema.
*/
int delete_file(char *filename)
{
  // Enpacota o comando delete
  package_delete(filename, buffer_write);
  int error_on_write_to_socket = write_str_to_socket(sock_g, buffer_write);
  if (error_on_write_to_socket < 0)
    return -1;

  int error_on_read_from_socket = read_until_eos(sock_g, buffer_read);
  if (error_on_read_from_socket < 0)
    return -1;

  int rval;
  char *rvalstr;
  if (response_unpack(buffer_read, &rval, &rvalstr) == NULL)
    return -1;

  // Removendo o arquivo do servidor, remove também da pasta local
  if (sync_set){
    TRY_LOCK_SCOPE(try_sync_mutex, {
      file_remove_from_sync_dir(filename);
    }, {
      file_remove_from_sync_dir(filename);
    });
  }

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
  close(sock_g);
  // fecha o programa
  exit(0);
}

/*
Envia uma mensagem de fechamento de socket para o servidor.
Finaliza o socket local com o servidor.
Retorna -1 caso tenha ocorrido algum erro.
*/
int close_connection()
{
  puts("\rExiting...");
  package_close(buffer_write);
  write_str_to_socket(sock_g, buffer_write);
  if (read_until_eos(sock_g, buffer_read) < 0)
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

// Verifica se o tipo de commando é "list"
int is_list_command(char *command_buffer)
{
  return strcmp(LIST, command_buffer) == 0;
}

// Verifica se o tipo de commando é "delete"
int is_delete_command(char *command_buffer)
{
  return strcmp(DELETE, command_buffer) == 0;
}

// Verifica se o tipo de commando é "download"
int is_download_command(char *command_buffer)
{
  return strcmp(DOWNLOAD, command_buffer) == 0;
}

// Verifica se o tipo de commando é "upload"
int is_upload_command(char *command_buffer)
{
  return strcmp(UPLOAD, command_buffer) == 0;
}

// Verifica se o tipo de commando é "get_sync_dir"
int is_get_sync_dir_command(char *command_buffer)
{
  return strcmp(GET_SYNC_DIR, command_buffer) == 0;
}

// Verifica se o tipo de commando é "exit"
int is_exit_command(char *command_buffer)
{
  return strcmp(EXIT, command_buffer) == 0;
}

int file_copy_to_sync_dir(char* source_file_path, char* dest_file_name)
{
  char dest_file_path[PATH_MAX];
  get_sync_dir_local_path(dest_file_path);

  sprintf(dest_file_path, "%s/%s", dest_file_path, dest_file_name);

  if(strcmp(source_file_path, dest_file_path) != 0){
    remove(dest_file_path);

    int source_fd = open(source_file_path, O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    int dest_fd = -1;
    if ((dest_fd = creat(dest_file_path, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0 || source_fd < 0)
  		return -1;

    char buffer_copy[1024];
    int readed;
    while((readed = read(source_fd, buffer_copy, sizeof(buffer_copy))) > 0){
      write(dest_fd, buffer_copy, readed);
    }
    close(source_fd);
    close(dest_fd);
  }
  return 0;
}

int file_remove_from_sync_dir(char* file_name)
{
    char current_working_path[PATH_MAX];
    char user_path[PATH_MAX];

    get_sync_dir_local_path(user_path);

    // Salva o diretório corrente
    if (getcwd(current_working_path, sizeof(current_working_path)) == NULL) {
      perror("Error on getting cwd:");
      return -1;
    }

    // Troca pro diretorio user_path
    if (chdir(user_path)) {
      perror("Error on change dir to sync_dir_<username>");
      return -1;
    }
    // remove o arquivo
    if (remove(file_name) != 0) {
      // perror("Error removing file from sync_dir:");
      return -1;
    }
    // Retorna diretorio para o local do executável
    if (chdir(current_working_path) < 0) {
      perror("Error returning from sync_dir:");
      return -1;
    }

    return 0;
}

/*
Inicia o processo de sincronização e faz a sincronização
inicial dos arquivos adicionados ao diretório "~/sync_dir_<username>"
antes de iniciar o aplicativo de cliente.
*/
int start_sync_monitor()
{
  if (!sync_set)
  {
    // obtém o caminho do usuário
    char user_path[PATH_MAX];
    get_sync_dir_local_path(user_path);
    // marca que o processo de sincronização está ativo
    sync_set = 1;
    // Faz a sincronização inicial para enviar os arquivos
    // que foram criados na pasta "sync_dir_<username>"
    // antes do processo de sincronização estar ativo.
    if (first_sync_local_files(user_path) < 0)
      return -1;
    // Sincronização inicial
    sync_client(username_g);
    pthread_create(&file_sync_thread, NULL, file_sync_monitor, NULL);
  }
  return 0;
}

/*
Faz a primeira sincronização dos arquivos locais com o servidor.
Lista todos os arquivos da pasta do parâmetros "user_path"
e verifica a existência deles no servidor. Caso não exista,
faz upload do arquivo.
*/
int first_sync_local_files(char *user_path)
{
  DIR *dir;
  struct dirent *ent;
  char file_name_send_buffer[PATH_MAX + 32];
  char response_buffer[10];
  //char file_names[MAXFILES][PATH_MAX];
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
        if (write_str_to_socket(sock_g, file_name_send_buffer) < 0)
          return -1;

        // recebe a resposta do servidor "true" ou "false"
        if (read_until_eos(sock_g, response_buffer) < 0)
          return -1;

        // "unpack" a resposta do servidor
        int response_status;
        char *unpacked_response;
        char *unpack_status = response_unpack(response_buffer, &response_status, &unpacked_response);

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

  return 0;
}

/*
Inicia uma instância do monitor de diretório com a api do inotify
e toma as decisões de fazer upload, deletar ou sincronizar os
arquivos da pasta "~/sync_dir_<username>".

Usa ao invés da informação de created e modified, a informação de
write_closed, e, a partir de um fim de escrita, envia arquivo ao servidor
Garante que, quando um arquivo vier como copiado, e gerar um evento de create,
mas ainda esteja escrevendo no arquivo, que o arquivo somente seja enviado
quando terminar de escrevê-lo.
*/
void *file_sync_monitor(void *param)
{
  char user_path[PATH_MAX];
  char filename[PATH_MAX];
  char time_modif[MAX_USERID];
  int length;
  int fd;
  int wd;
  char buffer[BUF_LEN];
  struct inotify_event *event;

  // localiza o diretório sync_dir_<username>
  get_sync_dir_local_path(user_path);

  // inicia um descritor de arquivo do inotify
  fd = inotify_init();

  if (fd < 0)
    perror("inotify_init");

  // abre um descritor de observação
  wd = inotify_add_watch(fd, user_path, IN_ALL_EVENTS);
  while ((length = read(fd, buffer, BUF_LEN)) > 0)
  {
    if (length < 0)
      perror("read");

    // TODO: Veririfcar pq o evento "create" está sendo lançado com o arquivo com 0 bytes
    //sleep(1);
    TRY_LOCK_SCOPE(try_sync_mutex,
    {
      // Percorre os eventos gerados do inotify
      for (int i = 0; i < length; i += EVENT_SIZE + event->len)
      {
        // localiza evento no buffer de eventos
        event = (struct inotify_event *)&buffer[i];
        if (event->len)
        {
          // Teste para eventos de criação,
          // deleção e modificação de arquivo
          int accessed = event->mask & IN_ACCESS;
          int attribute_modified = event->mask & IN_ATTRIB;
          int created = event->mask & IN_CREATE;
          int deleted = event->mask & IN_DELETE;
          int modified = event->mask & IN_MODIFY;
          int write_closed = event->mask & IN_CLOSE_WRITE;
          int not_write_closed = event->mask & IN_CLOSE_NOWRITE;
          int watch_dir_deleted = event->mask & IN_DELETE_SELF;
          int watch_dir_moved = event->mask & IN_MOVE_SELF;
          int moved_out = event->mask & IN_MOVED_FROM;
          int moved_in = event->mask & IN_MOVED_TO;
          int opened = event->mask & IN_OPEN;

          // Descobre o novo caminho do arquivo
          char filepath[PATH_MAX];
          sprintf(filepath, "%s/%s", user_path, event->name);

          // Se for escrito em um arquivo, envia ele
          // Se for deletado, deleta no servidor
          SCOPELOCK(file_sync_mutex, {
            if (write_closed || moved_in)
              send_file(filepath); /*sync_client(username_g);*/
            else if (deleted || moved_out)
              delete_file(event->name);
          });
        }
      }
    },
    // Caso o lock já tenha sido adquirido, zera o buffer de eventos
    {
      // Zero o buffer caso de eventos
      memset(buffer, 0, BUF_LEN);
    });

    // zera o buffer de eventos
    memset(buffer, 0, BUF_LEN);
  }

  // remove o observador do diretório do usuário
  // e fecha o descritor de arquivos
  inotify_rm_watch(fd, wd);
  close(fd);

  return NULL;
}

// Recebe o diretório "sync_dir_<username>" local
// do usuário pelo parâmetros de saída
void get_sync_dir_local_path(char *out_user_path)
{
  struct passwd *pw = getpwuid(getuid());
  sprintf(out_user_path, "%s/sync_dir_%s", pw->pw_dir, username_g);
}

#ifndef DROPBOXUTIL_H
#define DROPBOXUTIL_H

#include <time.h>
#include <limits.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <linux/if_link.h>
#include <arpa/inet.h>
#include <netdb.h>

#define USERNAME_MAX 64
#define MESSAGE_MAX 64
#define MAX_USERID 64
#define MAXFILES 256

#define MAX_SAME_USER 2

#define PORT 9000
#define BUF_SIZE 2048

#define COMPARE_EQUAL_STRING(str1, str2) (strcmp((str1), (str2)) == 0)
#define IS_IFACE_ETH(name) (strlen((name)) >= 3 && (name)[0] == 'e' && \
    ((name)[1] == 't' && (name)[2] == 'h') || ((name)[1] == 'n' && (name)[2] == 'p'))

// Mutex scope lock
#define SCOPELOCK(scope_mutex, scope)         \
    {                                         \
        pthread_mutex_lock((&scope_mutex));    \
        {scope;};                             \
        pthread_mutex_unlock((&scope_mutex));  \
    }
    
#define TRY_LOCK_SCOPE(scope_mutex, scope, elseScope)     \
    {                                                     \
        if (pthread_mutex_trylock((&scope_mutex)) == 0) { \
          {scope;};                                       \
          pthread_mutex_unlock((&scope_mutex));           \
        } else { {elseScope;}; }                          \
    }


typedef int bool;

#define TRUE 1
#define FALSE 0

enum BoolValues{
  true = TRUE,
  false = FALSE
};

struct file_info {
  char name[MAX_USERID];
  char extension[MAX_USERID];
  char last_modified[MAX_USERID];
  time_t last_modified_timestamp;
  int size;
  pthread_mutex_t reader_config;
  int readers;
  pthread_mutex_t can_use;
};

struct client {
  int devices[MAX_SAME_USER];
  char userid[MAX_USERID];
  char path_user[PATH_MAX];
  struct file_info files[MAXFILES];
  int logged_in;
  pthread_mutex_t config_mtx;
};

int read_until_eos_buffered(int sock, char * buffer);
int read_until_eos(int sock, char * buffer);
int read_n_from_socket(int n, int sock, char *buffer);
int write_str_to_socket(int sock, char * str);
int read_and_save_to_file(int sock, char * filename, int fsize);
int write_file_to_socket(int sock, char * filename, int fsize);

int read_int_from_socket(int sock, int* number);

#define write_int_to_socket(sockfd, number)         \
    {                                               \
        char number_str[16];                        \
        sprintf(number_str, "%d", (number));        \
        write_str_to_socket((sockfd), number_str);  \
    }   


struct PortAndFunc {
  int port;
  void* (*execute_client)(void* args);
};

// cria um tcp server e executa threads com uma função para tratar novas conexões
// executa uma thread para cada nova conexão, mas bloqueia essa função.
int execute_tcp_server_listener_block(int port, void* (*execute_client)(void* args));

// cria um tcp server e executa threads com uma função para tratar novas conexões
// executa uma thread para cada nova conexão e não bloqueia a execução.
pthread_t execute_tcp_server_listener_nonblock(int port, void* (*execute_client)(void* args));

// Executa um servidor TCP em segunda instância e aguada novas conexões para processar em outra thread
void* __execute_tcp_server_listener_nonblock(void* args);

// funções compartilhadas
int connect_server(char *host, int port);
int create_tcp_server(int port);

// executar função em outra thread sem precisar de um thread create
pthread_t async_executor(void* args, void*(*async_execute)(void* args));

/** 
 * Recebe por parâmetro de saída uma lista de ip's, separados por \n
 * atribuidos às interfaces ETHx e retorna a quantidade de ip's.
*/
int get_ip_list(char* ip_list);

void get_peer_ip_address(int sock, char* ip_buffer);

int is_socket_disconnected(int sockfd);

#endif /*DROPBOXUTIL_H*/
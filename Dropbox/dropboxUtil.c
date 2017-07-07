#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

#include "dropboxUtil.h"

int read_until_eos_buffered(int sock, char * buffer){
	int i = 0;
	int r = 0;
	char buffered[1024];
	int buffer_size = sizeof(buffered);
	do{
		r = read(sock, buffered, buffer_size);
		memcpy((buffer + i), buffered, r);

		if (r < 0)
			return -1;
		else if (r == 0)
			break;
		else if (r < buffer_size)
			break;

		i += r;

	}while(1);

	return i;
}

int read_n_from_socket(int n, int sock, char *buffer){
	int i = 0;
	int r = 0;

	while(i < n){
		r = read(sock, &buffer[i], 1);
		if(r < 0){
			return -1;
		}
		i+=r;
	}
}

int read_until_eos(int sock, char * buffer){
	int i = 0;
	int r = 0;

	do{
		r = read(sock, &buffer[i], 1);
		if (r < 0)
			return -1;
		i += r;
	}while(buffer[i-r]);

	return i;
}

int write_str_to_socket(int sock, char * str){
	int n;

	n = write(sock, str, strlen(str) + 1);
	return n;
}

int read_and_save_to_file(int sock, char * filename, int fsize){
	int f = -1;
	if ((f = creat(filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0)
		return -1;

	char buffer_copy[1250];

	int k=0, r;
	while(k < fsize){
		r = read(sock, buffer_copy, sizeof(buffer_copy));
		if (r < 0)
			break;

		//printf("%c", c);
		r = write(f, buffer_copy, r);
		if (r < 0){
			puts("Error writing file");
			close(f);
			return -1;
		}
		k += r;
	}

	// int k = 0, r;
	// char c;
	// while(k < fsize){
	// 	r = read(sock, &c, 1);
	// 	if (r < 0)
	// 		break;
	//
	// 	//printf("%c", c);
	// 	r = write(f, &c, 1);
	// 	if (r < 0){
	// 		puts("Error writing file");
	// 		close(f);
	// 		return -1;
	// 	}
	// 	k += r;
	// }
	// int sended;
	// if ((sended = sendfile(f, sock, NULL, fsize)) != fsize){
	// 	printf("Received: %d\n", sended);
	// 	close(f);
	// 	return -1;
	// }
	close(f);
	return 1;
}

int write_file_to_socket(int sock, char * filename, int fsize){
	int f = open(filename, O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if(f < 0){
		return -1;
	}
	int k = 0, r;
	//char c;

	char buffer_copy[1250];
	while(k < fsize){
		r = read(f, buffer_copy, sizeof(buffer_copy));
		if (r<0)
			break;
		r = write(sock, buffer_copy, r);
		if (r<0)
			break;
		k += r;
	}

	// while(k < fsize){
	// 	r = read(f, &c, 1);
	// 	if (r<0)
	// 		break;
	// 	r = write(sock, &c, 1);
	// 	if (r<0)
	// 		break;
	// 	k += r;
	// }
	// int sended;
	// if ((sended = sendfile(sock, f, NULL, fsize)) != fsize){
	// 	printf("Sended: %d, Size: %d, Error: %d\n", sended, fsize, errno);
	// 	close(f);
	// 	return -1;
	// }
	close(f);
	return 1;
}

// cria um tcp server e executa threads com uma função para tratar novas conexões
// executa uma thread para cada nova conexão, mas bloqueia essa função.
void execute_tcp_server_listener_block(int port, void* (*execute_client)(void* args))
{
	pthread_t execution_thread = execute_tcp_server_listener_nonblock(port, execute_client);
	pthread_join(execution_thread, NULL);
}

// cria um tcp server e executa threads com uma função para tratar novas conexões
// executa uma thread para cada nova conexão e não bloqueia a execução.
pthread_t execute_tcp_server_listener_nonblock(int port, void* (*execute_client)(void* args))
{
  // inicia a estrutura para passar como parâmetro
  struct PortAndFunc* portAndFuncArgs = (struct PortAndFunc*)malloc(sizeof(struct PortAndFunc));
  portAndFuncArgs->port = port;
  portAndFuncArgs->execute_client = execute_client;
  
  // executa uma thread para tratar o executor de escuta do servidor TCP
  return async_executor(portAndFuncArgs, __execute_tcp_server_listener_nonblock);
}

// Executa um servidor TCP em segunda instância e aguada novas conexões para processar em outra thread
void* __execute_tcp_server_listener_nonblock(void* args) {
  
	int returnStatus = 0;
	struct PortAndFunc portAndFuncArgs;
	
	// Obtém os argumentos fazendo um byte copy
	memcpy(&portAndFuncArgs, args, sizeof(struct PortAndFunc));
	free(args);
	
	int sockfd = create_tcp_server(portAndFuncArgs.port);
	if (sockfd < 0) {
		returnStatus = -1;
		pthread_exit(&returnStatus);
	}

	struct sockaddr_in cli_addr;
	socklen_t clilen = sizeof(struct sockaddr_in);
	
	while (1)
	{
		int *newsockfd = (int *)malloc(sizeof(int));
		*newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
		if (*newsockfd < 0)
		{
			printf("ERROR on accept");
			returnStatus = -1;
			pthread_exit(&returnStatus);
		}

		async_executor(newsockfd, portAndFuncArgs.execute_client);
	}

	close(sockfd);
	returnStatus = 0;
	pthread_exit(&returnStatus);
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
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
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
  if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    return -1;

  return sockfd;
}

int create_tcp_server(int port)
{
	int sockfd;
	struct sockaddr_in serv_addr, cli_addr;

	// Cria o socket para o servidor
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("ERROR opening socket\n");
		return -1;
	}

	// Habilita a reconexão ao socket pela mesma porta
	int enable = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
	{
		perror("setsockopt(SO_REUSEADDR) failed");
		return -1;
	}

	// Preenche a estrutura para criar um socket tcp
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	bzero(&(serv_addr.sin_zero), 8);

	// 
	if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		perror("ERROR on binding\n");
		return -1;
	}

	if (listen(sockfd, 10) < 0) {
		perror("ERROR on listening\n");
		return -1;
	}

	return sockfd;
}

// executar função em outra thread sem precisar de um thread create
pthread_t async_executor(void* args, void* (*async_execute)(void* args))
{
  pthread_t t;
  pthread_create(&t, NULL, async_execute, args);
  return t;
}

/** 
 * Recebe por parâmetro de saída uma lista de ip's, separados por \n
 * atribuidos às interfaces ETHx e retorna a quantidade de ip's.
*/
int get_ip_list(char* ip_list)
{
	// clear number and ip list string
	int ip_list_count = 0;
	ip_list[0] = '\0';

	struct ifaddrs *ifaddr = NULL, *ifa = NULL;
	int family, s, n;
	char host[NI_MAXHOST];
	
	if (getifaddrs(&ifaddr) == -1) {
		perror("getifaddrs");
		exit(EXIT_FAILURE);
	}

	/* Walk through linked list, maintaining head pointer so we
	can free list later */
	
	for (ifa = ifaddr, n = 0; ifa != NULL && ifa->ifa_addr != NULL; ifa = ifa->ifa_next, n++) {
		if (ifa->ifa_addr == NULL)
			continue;

       family = ifa->ifa_addr->sa_family;

       /* Display interface name and family (including symbolic
          form of the latter for the common families) */

       //printf("%-8s %s (%d)\n",
       //       ifa->ifa_name,
       //       (family == AF_PACKET) ? "AF_PACKET" :
       //       (family == AF_INET) ? "AF_INET" :
       //       (family == AF_INET6) ? "AF_INET6" : "???",
       //       family);

       /* For an AF_INET* interface address, display the address */

       if (family == AF_INET || family == AF_INET6) {
           s = getnameinfo(ifa->ifa_addr,
                   (family == AF_INET) ? sizeof(struct sockaddr_in) :
                                         sizeof(struct sockaddr_in6),
                   host, NI_MAXHOST,
                   NULL, 0, NI_NUMERICHOST);
           if (s != 0) {
               printf("getnameinfo() failed: %s\n", gai_strerror(s));
               exit(EXIT_FAILURE);
           }
           
           
			
			// se for ipv4 e a interface for eth? (regex) adiciona o host na lista
			if (family == AF_INET && IS_IFACE_ETH(ifa->ifa_name)) {
				// concatena um \n para separar caso mais de um ip
				// if (*ip_list_count > 0)
				// 	strcat(ip_list, "\n");
				// strcat(ip_list, host);
				// *ip_list_count = (*ip_list_count) + 1;
				
				// TODO: Create more ethernet interfaces support to send more ip address possibility
				strcpy(ip_list, host);
				ip_list_count = 1;
				return ip_list_count;
			}
			
           //printf("\t\taddress: <%s>\n", host);

       } else if (family == AF_PACKET && ifa->ifa_data != NULL) {
           struct rtnl_link_stats *stats = ifa->ifa_data;

           //printf("\t\ttx_packets = %10u; rx_packets = %10u\n"
           //       "\t\ttx_bytes   = %10u; rx_bytes   = %10u\n",
           //       stats->tx_packets, stats->rx_packets,
           //       stats->tx_bytes, stats->rx_bytes);
       }
   }

   freeifaddrs(ifaddr);
}
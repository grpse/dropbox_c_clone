#include "dropboxServer.h"

pthread_t time_server_thread;

int main(int argc, char *argv[])
{
	assert(argc == 2);

	if (argc < 2)
		printf("usage: %s <port>\n", argv[0]);

	init_users();

	int port = atoi(argv[1]);
	int port_time_server = port + 1;
	int sockfd = create_tcp_server(port);
	if (sockfd < 0)
		exit(-1);

	// Create time server process
	pthread_create(&time_server_thread, NULL, time_server, (void*)&port_time_server);

	struct sockaddr_in cli_addr;
	socklen_t clilen = sizeof(struct sockaddr_in);

	while (1)
	{
		int *newsockfd = (int *)malloc(sizeof(int));
		*newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
		if (*newsockfd < 0)
		{
			printf("ERROR on accept");
			exit(1);
		}

		pthread_t thc;
		pthread_create(&thc, NULL, client_process, newsockfd);
	}

	// close socket and terminate time server
	pthread_cancel(time_server_thread);
	close(sockfd);
	return 0;
}

void* time_server(void* port_ptr) 
{
	int port = *(int*)port_ptr;
	int time_server_socket = create_tcp_server(port);

	printf("Time server started!\n");

	while(1)
	{
		struct sockaddr_in cli_addr;
		socklen_t clilen = sizeof(struct sockaddr_in);
		int *newsockfd = (int *)malloc(sizeof(int));
		*newsockfd = accept(time_server_socket, (struct sockaddr *)&cli_addr, &clilen);		

		if (*newsockfd < 0)
		{
			perror("ERROR on connecting to time server\n");
			exit(-1);
		}

		pthread_t time_server_client_thread;
		pthread_create(&time_server_client_thread, NULL, time_server_client_process, newsockfd);
	}

	close(time_server_socket);
}

void* time_server_client_process(void* sock_ptr)
{
	// declare variables
	int sockfd = *(int*)sock_ptr;
	char buffer[256];

	free(sock_ptr);

	// read current time, write to buffer,
	// send via socket and close it	
	sprintf(buffer, "%lu", time(0));
	write_str_to_socket(sockfd, buffer);
	close(sockfd);
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
#include "dropboxServer.h"

pthread_t time_server_thread;
pthread_t replica_sends_heart_beat;
pthread_t replica_receives_heart_beat;

#define COMPARE_EQUAL_STRING(str1, str2) strcmp((str1), (str2)) == 0

int main(int argc, char *argv[])
{
	assert(argc >= 3);

	if (argc < 3) 
	{
		printf("usage: %s [main|replica <main host>] <port>\n", argv[0]);
		return 1;
	}
	
	char* type = argv[1];
	
	if (COMPARE_EQUAL_STRING(type, "main"))
	{
		int port = atoi(argv[2]);
		int port_time_server = port + 1;
		// Create time server process
		pthread_create(&time_server_thread, NULL, time_server, (void*)&port_time_server);
	
		int execution_status = wait_for_dropbox_client_connections(port);
		// se ocorreu uma falha ...
		if (execution_status < 0) return -1;	
	}
	else if (COMPARE_EQUAL_STRING(type, "replica"))
	{
		int ip_list_count;
		char* ip_list[10];
		get_ip_list((char**)ip_list, &ip_list_count);
		
		printf("%s\n", ip_list[0]);
		printf("%d\n", ip_list_count);
	}
	
	return 0;
}

int start_as_replica_server(char* main_host, int main_port)
{
	
}

int wait_for_dropbox_client_connections(int wait_port)
{
	init_users();

	int sockfd = create_tcp_server(wait_port);
	if (sockfd < 0)
		exit(-1);

	struct sockaddr_in cli_addr;
	socklen_t clilen = sizeof(struct sockaddr_in);

	while (1)
	{
		int *newsockfd = (int *)malloc(sizeof(int));
		*newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
		if (*newsockfd < 0)
		{
			printf("ERROR on accept");
			return -1;
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



void get_ip_list(char** ip_list, int* ip_list_count)
{
	*ip_list_count = 0;
	struct ifaddrs *ifaddr, *ifa;
	int family, s, n;
	char host[NI_MAXHOST];
	
	if (getifaddrs(&ifaddr) == -1) {
		perror("getifaddrs");
		exit(EXIT_FAILURE);
	}

	/* Walk through linked list, maintaining head pointer so we
	can free list later */
	
	for (ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++) {
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
			if (family == AF_INET && (COMPARE_EQUAL_STRING(ifa->ifa_name, "eth0") || COMPARE_EQUAL_STRING(ifa->ifa_name, "eth1"))) {
				ip_list[*ip_list_count] = (char*)malloc(sizeof(char) * strlen(host) + 2);
				strcpy(ip_list[*ip_list_count], host);
				*ip_list_count = *ip_list_count + 1;
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
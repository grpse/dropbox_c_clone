#include "dropboxServer.h"
#include "dropboxRM.h"

pthread_mutex_t receive_ip_mutex;

#define MAX_REPLICA_SOCKETS 128
int replica_sockets[MAX_REPLICA_SOCKETS];
int replica_sockets_index = 0;

char replicas_ip_list[4096] = "";
char clients_ip_list[4096] = "";
int last_replica_order = 0;
int main_port = 0;

int main(int argc, char *argv[])
{
	assert(argc >= 3);
	
	if (argc < 3) 
	{
		printf("usage: %s [main|replica <main host>] <port>\n", argv[0]);
		return 1;
	}
	
	init_users();
	
	char* type = argv[1];
	
	if (COMPARE_EQUAL_STRING(type, "main"))
	{
		main_port = atoi(argv[2]);
		pthread_t main_process = start_all_main_services_starting_at_port(main_port);
		pthread_join(main_process, NULL);
	}
	else if (COMPARE_EQUAL_STRING(type, "replica"))
	{
		char* main_host = argv[2];
		main_port = atoi(argv[3]);
		int execution_status = start_as_replica_server(main_host, main_port);
		// se ocorreu uma falha ...
		if (execution_status < 0) return -1;
	}
	
	pthread_exit(NULL);
	
	return 0;
}

pthread_t start_all_main_services_starting_at_port(int main_port)
{
	int port = MAIN_PORT;
	int port_time_server = TIME_PORT;
	int port_update_replicas = HEART_PORT;
	int port_replication = REPLICATION_PORT;
	
	printf("MAIN_PORT: %d\n", port);
	printf("TIME_PORT: %d\n", port_time_server);
	printf("REPL_PORT: %d\n", port_update_replicas);
	printf("FILE_PORT: %d\n", port_replication);
	
	// Create time server process
	execute_tcp_server_listener_nonblock(port_time_server, time_server_client_process);
	// create update replicas port
	execute_tcp_server_listener_nonblock(port_update_replicas, replicas_update_ips_list);
	// Create a replication server to replicas connect to receive files
	execute_tcp_server_listener_nonblock(port_replication, )
	// start dropbox wait service
	return execute_tcp_server_listener_nonblock(port, client_intermediate_process);
}

void* client_intermediate_process(void* args)
{
	int client_sock = *(int*)args;
	char client_ip[32];
	
	// get ip address of a client from socket
	get_peer_ip_address(client_sock, client_ip);
	
	// update clients ip list
	SCOPELOCK(receive_ip_mutex, {
		strcat(clients_ip_list, client_ip);
		strcat(clients_ip_list, "\n");
	});
	
	// start receiving commands from client
	client_process(args);
}

void* replicas_update_ips_list(void* args)
{
	char message_buffer[4096];
	int exit_status = 0;
	int sockfd = *(int*)args;
	
	// enable the possibility to cancel this thread
	int s1 = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	int s2 = pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	
	SCOPELOCK(receive_ip_mutex, {
		// copy new replica ip to the list
		read_until_eos(sockfd, message_buffer);

		if (!COMPARE_EQUAL_STRING(message_buffer, "get_replicas_ip_list"))
		{
			char replica_order[16];
			sprintf(replica_order, "%d", ++last_replica_order);
			strcat(replicas_ip_list, replica_order);
			strcat(replicas_ip_list, ":");
			strcat(replicas_ip_list, message_buffer);
			strcat(replicas_ip_list, "\n");
			
			printf("ip_list: \n%s\n", replicas_ip_list);

			// send replica it's order
			write_int_to_socket(sockfd, last_replica_order);
			printf("Next replica order: %d\n", last_replica_order);
		}
		else
		{
			// if already ready a replica connection, but reconnecting from here
			strcpy(message_buffer, clients_ip_list);
			write_str_to_socket(sockfd, message_buffer);
		}
	});
	
	while(true)
	{
		// receive a update command from replicas
		// TODO: make server wait to read
		int read_return = read_until_eos(sockfd, message_buffer);
		
		SCOPELOCK(receive_ip_mutex, {
			if (COMPARE_EQUAL_STRING(message_buffer, "get_clients_ip_list"))
				strcpy(message_buffer, clients_ip_list);
			
			if (COMPARE_EQUAL_STRING(message_buffer, "get_replicas_ip_list"))
				strcpy(message_buffer, replicas_ip_list);
		});
	
		printf("Send buffer: \n%s\n", message_buffer);
	
		// send update to replica		
		int write_return = write_str_to_socket(sockfd, message_buffer);

		if (read_return < 0 || write_return <0 || errno != 0)
		{
			perror("Error sending update:");
			exit_status = -1;
			break;
		}
	}
	
	pthread_exit((void*)&exit_status);
}

void* replication_server(void* args)
{
	// declare variables
	int sockfd = *(int*)sock_ptr;
	
	replica_sockets_index = ++replica_sockets_index % MAX_REPLICA_SOCKETS;
	replica_sockets[replica_sockets_index] = sockfd;
	
	
}

int start_replica_transaction(char* command, char* username, char* filename, char* modtime, int filesize )
{

	

	return 1;
}

int replica_file_get_copy_buffer(char* buffer, int size)
{

	return 1;
}

int commit_replica_transaction(char* command)
{

	return 1;
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
#include "dropboxServer.h"

pthread_t time_server_thread;
pthread_t receive_replica_files_thread;
pthread_t receive_clients_connected_thread;
pthread_mutex_t receive_ip_mutex;

char replicas_ip_list[4096] = "";
char clients_ip_list[4096] = "";
int last_replica_order = 0;
int main_port = 0;

#define MAIN_PORT main_port
#define TIME_PORT main_port + 1
#define HEART_PORT main_port + 2
#define REPLICATION_PORT main_port + 3

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
	
	printf("MAIN_PORT: %d\n", port);
	printf("TIME_PORT: %d\n", port_time_server);
	printf("REPL_PORT: %d\n", port_update_replicas);
	
	// Create time server process
	execute_tcp_server_listener_nonblock(port_time_server, time_server_client_process);
	// create update replicas port
	execute_tcp_server_listener_nonblock(port_update_replicas, replicas_update_ips_list);
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

int start_as_replica_server(char* main_host, int main_port)
{
	
	int heart_beat_port = HEART_PORT;
	int sockfd = connect_server(main_host, heart_beat_port);


	printf("host:port > %s:%d\n", main_host, heart_beat_port);
	printf("Socket: %d\n", sockfd);

	// TODO: Start file replica server to accept main server files transaction
	pthread_t replication_thread = execute_tcp_server_listener_nonblock(REPLICATION_PORT, receive_replica_files);

	// send my ips to server
	int my_order = -1;
	char my_ip[16];
	get_my_ip(my_ip);
	write_str_to_socket(sockfd, my_ip);
	printf("Start Replica. My ip: %s\n", my_ip);
	// receive my order number as main server
	receive_my_replica_order(sockfd, &my_order);
	printf("My order on replicas list: %d\n", my_order);

	struct ReplicasUpdateVerifyParams* params = (struct ReplicasUpdateVerifyParams*)calloc(1, sizeof(struct ReplicasUpdateVerifyParams));
		
	params->main_host = main_host;
	params->main_port = main_port;
	params->sockfd = sockfd;
	params->my_order = my_order;
	strcpy(params->next_host, params->main_host);

	while (true)
	{			
		// pass to next 2 threads the same environment (host, port, socket and disconnection status(to close update thread on disconnection))		
		pthread_t update_replica_thread = params->update_thread = async_executor(params, update_replicas_and_clients_ip_list);
		pthread_t verify_socket_disconnection = async_executor(params, verifying_disconnection_to_reconnect_or_turn_it_main_server);
		
		// wait last thread to finish
		pthread_join(verify_socket_disconnection, (void**)&params);

		// become master
		if (params == NULL) 
		{
			printf("Me turning into main....\n");
			free(params);
			break;
		}
		else 
		{
			printf("Me connecting to the new server...\"%s\"\n", params->next_host);
			sleep(5);
			params->sockfd = sockfd = connect_server(params->next_host, heart_beat_port);
			
		}

		pthread_cancel(update_replica_thread);
	}
	
	printf("becoming master...\n");
	
	// stop receiving replica files
	pthread_cancel(replication_thread);
	
	// start dropbox server
	pthread_t wait_for_dropbox_thread = start_all_main_services_starting_at_port(main_port);
	
	//TODO: let all files available to clients
	//TODO: send all clients ip a message to connecto to me
	
	// block dropbox server execution
	// wait for return
	int dropbox_server_return_status;
	pthread_join(wait_for_dropbox_thread, (void*)&dropbox_server_return_status);
	
	return dropbox_server_return_status;
}

void* update_replicas_and_clients_ip_list(void* replicasUpdateVerifyParams)
{
	struct ReplicasUpdateVerifyParams params = *(struct ReplicasUpdateVerifyParams*)replicasUpdateVerifyParams;

	int sockfd = params.sockfd;
	// enable the possibility to cancel this thread
	int s1 = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	int s2 = pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	printf("this_thread_is_now_cancelable\n");
	
	while(true)
	{
		// receives server replicas ip lists update
		char message_buffer[4096] = "get_replicas_ip_list";
		int write_return = write_str_to_socket(sockfd, message_buffer);
		int read_return = read_until_eos(sockfd, message_buffer);
		
		// update replicas ip lists
		SCOPELOCK(receive_ip_mutex, {
			strcpy(replicas_ip_list, message_buffer);
			last_replica_order = get_new_last_order(replicas_ip_list);
		});
		
		printf("local_replicas_ip_list: \n%s\n", replicas_ip_list);
		
		// receives clients ip list update		
		strcpy(message_buffer, "get_clients_ip_list");
		write_return = write_str_to_socket(sockfd, message_buffer);
		read_return = read_until_eos(sockfd, message_buffer);
		
		// update clients ip list
		SCOPELOCK(receive_ip_mutex, {
			strcpy(clients_ip_list, message_buffer);
		});
		
		printf("local_clients_ip_list: \n%s\n", clients_ip_list);

		sleep(5);
	}
}

void* verifying_disconnection_to_reconnect_or_turn_it_main_server(void* replicasUpdateVerifyParams)
{
	struct ReplicasUpdateVerifyParams* params = (struct ReplicasUpdateVerifyParams*)replicasUpdateVerifyParams;
	
	char* main_host = params->main_host;
	int main_port = params->main_port;
	int sockfd = params->sockfd;
	int my_order = params->my_order;
	pthread_t update_replicas_thread = params->update_thread;
	int heart_beat_port = main_port + 2;
	
	
	printf("main_host_copy: %s\n", main_host);
	printf("main_port_copy: %d\n", main_port);
	printf("sockfd: %d\n", sockfd);
	printf("update_thread: %d\n", update_replicas_thread);
	printf("my_order: %d\n", my_order);
	
	while(1)
	{
		if (is_socket_disconnected(sockfd))
		{
			// ... some error
			//  1 - verify next on server list
			//	2 - 
			//		2.1 - if it's me, break this while loop and start waiting for dropbox connections
			//		2.2 - if not, wait 5 seconds and try connect to the new main server
			bool its_my_time = false;
			
			SCOPELOCK(receive_ip_mutex, {
				// copy first ip address and test order verify if it's not mine
				int next_in_order;
				sscanf(replicas_ip_list, "%d", &next_in_order);
				
				printf("My and next order: %d == %d\n", my_order, next_in_order);
				
				if (my_order == next_in_order)
				{
					its_my_time = true;					
				}
				
				if (strlen(replicas_ip_list) > 3)
				{
					printf("ips: \n%s\n", replicas_ip_list);
					char *ip_start = strstr(replicas_ip_list, ":") + 1;
					char *ip_end = strstr(replicas_ip_list, "\n");
					
					printf("start: %s\n", ip_start);
					printf("end: %s\n", ip_end);
					
					int ip_str_len = ip_end - ip_start;
					printf("ip_str_len: %d\n", ip_str_len);
					char next_ip[32];
					
					memcpy(next_ip, ip_start, ip_str_len);
					next_ip[ip_str_len] = '\0';

					strcpy(params->next_host, next_ip);
					printf("next ip: \"%s\"\n", params->next_host);
				}
				
			});

			if (its_my_time)
			{
				SCOPELOCK(receive_ip_mutex, {
					// update replicas_ip_list
					// only new main server can update replicas list to pass to other replicas
					char *ip_end = strstr(replicas_ip_list, "\n");
					char *rest_replicas_ip_list = (ip_end + 1);
					char replicas_temp[4096] = "";
					memcpy(replicas_temp, rest_replicas_ip_list, strlen(rest_replicas_ip_list));
					replicas_temp[strlen(rest_replicas_ip_list)] = '\0';
					

					strcpy(replicas_ip_list, replicas_temp);
					printf("ip_list_now:\n\"%s\"\n", replicas_temp);
					printf("ip_list_now:\n\"%s\"\n", replicas_ip_list);

					// update last replica order
				});

				pthread_exit(NULL);
			}
			else
			{
				pthread_exit(params);
			}
		}
	}
}

int get_new_last_order(char* replicas_ip_list)
{
	int new_last_order = -1;
	// update last replica order					
	int i;
	for (i = strlen(replicas_ip_list) - 1; i >= 0; i--) 
	{
		if (replicas_ip_list[i] == ':')
		{
			i--;
			break;
		}
	}
	
	char *before_last_id = &replicas_ip_list[i];					
	printf("before_last_id:  \"%s\"\n", before_last_id);
	sscanf(before_last_id, "%d\n", &new_last_order);
	printf("new last order: %d\n", new_last_order);
	return new_last_order;
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

void* receive_replica_files(void* args)
{
	char message_buffer[4096];
	
	int sockfd = *(int*)args;
	
	while(1)
	{
		// receive file info
		if (read_until_eos(sockfd, message_buffer) < 0)
		{
			// error occured, finalize thread
		}
		else
		{
			// receive file data
		}	
	}
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
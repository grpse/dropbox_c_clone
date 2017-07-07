#include "dropboxServer.h"

pthread_t time_server_thread;
pthread_t receive_replica_files_thread;
pthread_t receive_clients_connected_thread;

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
		int port = atoi(argv[2]);
		int port_time_server = port + 1;
		// // Create time server process
		// time_server_thread = async_executor(&port_time_server, time_server);
	
		// int execution_status = wait_for_dropbox_client_connections(port);
		// // se ocorreu uma falha ...
		// if (execution_status < 0) return -1;
		execute_tcp_server_listener_nonblock(port_time_server, time_server_client_process);
		execute_tcp_server_listener_block(port, client_process);
	}
	else if (COMPARE_EQUAL_STRING(type, "replica"))
	{
		int ip_list_count = 0;
		char ip_list[256];
		
		ip_list_count = get_ip_list(ip_list);
		
		printf("%s\n", ip_list);
		printf("%d\n", ip_list_count);
		return 0;
		
		char* main_host = argv[2];
		int main_port = atoi(argv[3]);
		int execution_status = start_as_replica_server(main_host, main_port);
		// se ocorreu uma falha ...
		if (execution_status < 0) return -1;
	}
	
	pthread_exit(NULL);
	
	return 0;
}

int start_as_replica_server(char* main_host, int main_port)
{
	int heart_beat_port = main_port + 2;
	int sockfd = connect_server(main_host, heart_beat_port);
	
	if (sockfd > 0)
	{
		// send my ips to server
		
		// receive my order number as main server
		
		// start waiting, in another thread, for files replica requests
		receive_replica_files_thread = async_executor(&sockfd, receive_replica_files);
		//pthread_create(&receive_replica_files_thread, NULL, receive_replica_files, (void*)&sockfd);
		
		while(1)
		{
			char message_buffer[4096] = "get_servers_list";
			sleep(5);
			// receives servers ip lists update
			if (write_str_to_socket(sockfd, message_buffer) < 0 || read_until_eos_buffered(sockfd, message_buffer) < 0)
			{
				// ... some error
				//  1 - verify next on server list
				//	2 - 
				//		2.1 - if it's me, break this while loop and start waiting for dropbox connections
				//		2.2 - if not, wait 5 seconds and try connect to the new main server
			}
			else
			{
				// update internal ip lists of replica servers
			}
		}
	}
	
	return wait_for_dropbox_client_connections(main_port);
}

void* receive_replica_files(void* args)
{
	char message_buffer[4096];
	
	int sockfd = *(int*)args;
	
	// receive file info
	if (read_until_eos_buffered(sockfd, message_buffer) < 0)
	{
		// error occured, finalize thread
	}
	else {
		// receive file data
	}
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
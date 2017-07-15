#include "dropboxRM.h"

pthread_mutex_t receive_ip_mutex;

extern char replicas_ip_list[4096];
extern char clients_ip_list[4096];
extern int last_replica_order;
extern int main_port;

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
				else
				{
				    its_my_time = true;
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
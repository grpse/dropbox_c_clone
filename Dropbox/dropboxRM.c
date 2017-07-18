#include "dropboxRM.h"

pthread_mutex_t receive_ip_mutex;

extern char replicas_ip_list[4096];
extern char clients_ip_list[4096];
extern int last_replica_order;
extern int main_port;
char my_ip[64];

int start_as_replica_server(char *main_host, int main_port)
{

	int heart_beat_port = HEART_PORT;
	int sockfd = connect_server(main_host, heart_beat_port);

	printf("host:port > %s:%d\n", main_host, heart_beat_port);
	printf("Socket: %d\n", sockfd);

	// send my ips to server
	int my_order = -1;
	char my_ip[16];
	get_my_ip(my_ip);
	write_str_to_socket(sockfd, my_ip);
	printf("Start Replica. My ip: %s\n", my_ip);
	// receive my order number as main server
	receive_my_replica_order(sockfd, &my_order);
	printf("My order on replicas list: %d\n", my_order);

	// struct ReplicasUpdateVerifyParams* params = (struct ReplicasUpdateVerifyParams*)calloc(1, sizeof(struct ReplicasUpdateVerifyParams));
	struct ReplicasUpdateVerifyParams *params = MALLOC1(struct ReplicasUpdateVerifyParams);
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
		pthread_t replication_connection_handler = async_executor(params, receive_replica_files);

		// wait last thread to finish
		pthread_join(verify_socket_disconnection, (void **)&params);

		// become master
		if (params == NULL)
		{
			printf("Me turning main....\n");
			free(params);
			break;
		}
		else // connect to the next host
		{
			printf("Me connecting to the new server...\"%s\"\n", params->next_host);
			sleep(5);
			params->sockfd = sockfd = connect_server(params->next_host, heart_beat_port);
		}

		// cancel threads
		pthread_cancel(replication_connection_handler);
		pthread_cancel(update_replica_thread);
	}

	printf("becoming master...\n");

	// start dropbox server
	pthread_t wait_for_dropbox_thread = start_all_main_services_starting_at_port(main_port);

	//send all clients ip a message to connecto to me
	send_all_clients_my_ip(my_ip);

	// block dropbox server execution
	// wait for return
	int dropbox_server_return_status;
	pthread_join(wait_for_dropbox_thread, (void *)&dropbox_server_return_status);

	return dropbox_server_return_status;
}

void send_all_clients_my_ip(char* my_ip)
{
	char client_ip[64];

	char* next_client_ip;
	char temp_clients_ip_list[PATH_MAX];

	SCOPELOCK(receive_ip_mutex, {
		strcpy(temp_clients_ip_list, clients_ip_list);
	});
	printf("starting to send reconnection to clients\n");
	next_client_ip = strstr(temp_clients_ip_list,"\n");
	printf("next_ip_ '%s'\n", next_client_ip);

	while(true)
	{
		
		if (next_client_ip == NULL) break; 

		size_t client_ip_len = next_client_ip - &temp_clients_ip_list[0];
		memcpy(client_ip, temp_clients_ip_list, client_ip_len);
		client_ip[client_ip_len] = '\0';
		temp_clients_ip_list[client_ip_len] = '\0';

		SCOPELOCK(receive_ip_mutex, {
			strcpy(clients_ip_list, (next_client_ip + 1));
		});

		printf("next client ip: '%s'\n", client_ip);

		int client_socket = connect_server(client_ip, CLIENT_RECONNECT_PORT);
		
		if (write_str_to_socket(client_socket, my_ip) < 0)
		{
			printf("ERROR TRYING TO MAKE CLIENTS RECONNECT\n");
			exit(1);
		}

		next_client_ip = strstr(temp_clients_ip_list,"\n");
	}
}

void *receive_replica_files(void *args)
{
	printf("ready to receive files!\n");

	// enable the possibility to cancel this thread
	int s1 = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	int s2 = pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	printf("this_thread_is_now_cancelable\n");

	// receive next host ip address and connect to it
	struct ReplicasUpdateVerifyParams *params = (struct ReplicasUpdateVerifyParams *)args;
	int sockfd = connect_server(params->next_host, REPLICATION_PORT);

	printf("receive files from:  %s:%d\n", params->next_host, REPLICATION_PORT);
	printf("receive files socket: %d\n\n\n", sockfd);

	char message_buffer[4096];
	bool is_receiving_file = false;
	bool is_deleting_file = false;

	while (true)
	{
		// receive new command from server
		if (read_until_eos(sockfd, message_buffer) < 0)
			break;

		// receive file
		if (COMPARE_EQUAL_STRING(message_buffer, REPLICATE_FILE))
		{
			char so_command[PATH_MAX];
			char username[MAX_USERID];
			char filename[PATH_MAX];
			char stage_filepath[PATH_MAX];
			char modtime[MODTIME_MAX];
			char *tmp_filename;
			char *tmp_modtime;
			int filesize;
			bool success_receiving = true;

			// receive user name
			if (read_until_eos(sockfd, message_buffer) < 0)
				break;
			strcpy(username, message_buffer);

			// receive file info
			if (read_until_eos(sockfd, message_buffer) < 0)
				break;

			// store file info
			if (get_file_info(message_buffer, &tmp_filename, &tmp_modtime, &filesize) == NULL)
				break;

			// copy filename and modfication time
			strcpy(filename, tmp_filename);
			strcpy(modtime, tmp_modtime);

			printf("receiving file: '%s'\n", filename);

			// get stagging file path
			path_join(stage_filepath, STAGGING_FILE_PATH, username);
			// create destination dir
			char* mkdircmd = "mkdir -p ";
			strcpy(so_command, mkdircmd);
			strcat(so_command, stage_filepath);
			system(so_command);
			strcpy(so_command, mkdircmd);
			strcat(so_command, SERVER_FILES);
			strcat(so_command, username);
			system(so_command);
			printf("executing command: \"%s\"\n", so_command);

			// append filename
			strcat(stage_filepath, filename);

			//  wait for start data section
			if (read_until_eos(sockfd, message_buffer) < 0)
				break;

			if (COMPARE_EQUAL_STRING(message_buffer, START_DATA_SECTION))
			{				
				//verify if reading from socket and modify mod time was successful				
				success_receiving = read_and_save_to_file(sockfd, stage_filepath, filesize) >= 0;
				success_receiving = success_receiving && modify_file_time(stage_filepath, modtime);
				printf("file received success? %d\n", success_receiving);
			}

			// wait for end data section
			if (read_until_eos(sockfd, message_buffer) < 0)
				break;

			printf("received the last command: %s\n", message_buffer);
			
			if (COMPARE_EQUAL_STRING(message_buffer, END_DATA_SECTION))
			{
				char *message_back = success_receiving ? "success" : "fail";
				
				// send message back if it was success or fail and copy
				// to user folder or remove from stagging area
				if (write_str_to_socket(sockfd, message_back) >= 0 && success_receiving)
					commit_replicated_file_to_user_folder(stage_filepath, username, filename);
				else
					remove(stage_filepath);

				printf("commited file: '%s'\n", filename);
				
			}

			if (COMPARE_EQUAL_STRING(message_buffer, ROLLBACK))
				remove(stage_filepath);
		}
		else if (COMPARE_EQUAL_STRING(message_buffer, DELETE_FILE))
		{
			printf("DELETING FILE...\n");
			char username[MAX_USERID];
			char filename[PATH_MAX];
			bool success_receiving = true;

			// receive user name
			if (read_until_eos(sockfd, message_buffer) < 0)
				break;
			strcpy(username, message_buffer);

			printf("DELETE PROCESS FROM USER: '%s'\n", username);

			// receive file info
			if (read_until_eos(sockfd, message_buffer) < 0)
				break;

			// copy filename
			strcpy(filename, message_buffer);

			printf("DELETE PROCESS FILE: '%s'\n", filename);
			//  wait for start data section
			if (read_until_eos(sockfd, message_buffer) < 0)
				break;

			if (COMPARE_EQUAL_STRING(message_buffer, START_DATA_SECTION))
			{
				// receive file name then wait for commit for deletion
				if (read_until_eos(sockfd, message_buffer) < 0)
					break;

				printf("file to delete: %s\n", filename);
			}

			// wait for end data section
			if (read_until_eos(sockfd, message_buffer) < 0)
				break;

			printf("received the last command: %s\n", message_buffer);
			
			if (COMPARE_EQUAL_STRING(message_buffer, END_DATA_SECTION))
			{
				// delete file
				// go to the right path
				char filepath[PATH_MAX];
				sprintf(filepath, "%s%s/%s", SERVER_FILES, username, filename);				
				remove(filepath);

				printf("File to delete: '%s'\n", filepath);
			}

			if (COMPARE_EQUAL_STRING(message_buffer, ROLLBACK))
			{
				// nothign to do
			}
		}
	}
}

void commit_replicated_file_to_user_folder(char *stage_filepath, char *username, char *filename)
{


	char user_server_filepath[PATH_MAX] = "";
	path_join(user_server_filepath, SERVER_FILES, username);
	strcat(user_server_filepath, filename);

	printf("copying file: '%s' to '%s'\n", stage_filepath, user_server_filepath);

	file_copy(stage_filepath, user_server_filepath);

	printf("commited file to: '%s'\n", user_server_filepath);
}

void *update_replicas_and_clients_ip_list(void *replicasUpdateVerifyParams)
{
	// enable the possibility to cancel this thread
	int s1 = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	int s2 = pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	printf("this_thread_is_now_cancelable\n");

	struct ReplicasUpdateVerifyParams params = *(struct ReplicasUpdateVerifyParams *)replicasUpdateVerifyParams;

	int sockfd = params.sockfd;

	while (true)
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

		//printf("local_replicas_ip_list: \n%s\n", replicas_ip_list);

		// receives clients ip list update
		strcpy(message_buffer, "get_clients_ip_list");
		write_return = write_str_to_socket(sockfd, message_buffer);
		read_return = read_until_eos(sockfd, message_buffer);

		// update clients ip list
		SCOPELOCK(receive_ip_mutex, {
			strcpy(clients_ip_list, message_buffer);
		});

//		printf("local_clients_ip_list: \n%s\n", clients_ip_list);
		sleep(1);
	}
}

void *verifying_disconnection_to_reconnect_or_turn_it_main_server(void *replicasUpdateVerifyParams)
{
	struct ReplicasUpdateVerifyParams *params = (struct ReplicasUpdateVerifyParams *)replicasUpdateVerifyParams;

	char *main_host = params->main_host;
	int main_port = params->main_port;
	int sockfd = params->sockfd;
	int my_order = params->my_order;
	pthread_t update_replicas_thread = params->update_thread;
	int heart_beat_port = main_port + 2;

	printf("main_host_copy: %s\n", main_host);
	printf("main_port_copy: %d\n", main_port);
	printf("sockfd: %d\n", sockfd);
	printf("update_thread: %lu\n", update_replicas_thread);
	printf("my_order: %d\n", my_order);

	while (1)
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

					// save my_ip
					size_t ip_len = ip_end - &replicas_ip_list[0];
					memcpy(my_ip, replicas_ip_list, ip_len);
					my_ip[ip_len] = '\0';
					printf("my_ip: \"%s\"\n", my_ip);

					memcpy(replicas_temp, rest_replicas_ip_list, strlen(rest_replicas_ip_list));
					replicas_temp[strlen(rest_replicas_ip_list)] = '\0';

					strcpy(replicas_ip_list, replicas_temp);
					printf("ip_list_now:\n\"%s\"\n", replicas_temp);
					printf("ip_list_now:\n\"%s\"\n", replicas_ip_list);

					// TODO: update last replica order
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

int get_new_last_order(char *replicas_ip_list)
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
	// printf("before_last_id:  \"%s\"\n", before_last_id);
	sscanf(before_last_id, "%d\n", &new_last_order);
	// printf("new last order: %d\n", new_last_order);
	return new_last_order;
}
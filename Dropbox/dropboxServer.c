#include "dropboxServer.h"
#include "dropboxRM.h"

pthread_mutex_t receive_ip_mutex;
pthread_mutex_t replica_socket_mutex;

enum TransactionStatus last_replication_transaction_status;

#define MAX_REPLICA_SOCKETS 128
#define MAX_REPLICA_IP_LIST 4096
int replica_sockets[MAX_REPLICA_SOCKETS];
int replica_sockets_index;

char replicas_ip_list[MAX_REPLICA_IP_LIST];
char clients_ip_list[MAX_REPLICA_IP_LIST];
int last_replica_order;
int main_port;

int main(int argc, char *argv[])
{
	assert(argc >= 3);

	if (argc < 3)
	{
		printf("usage: %s [main|replica <main host>] <port>\n", argv[0]);
		return 1;
	}

	// init global variables
	bzero(replicas_ip_list, MAX_REPLICA_IP_LIST);
	bzero(clients_ip_list, MAX_REPLICA_IP_LIST);
	last_replica_order = 0;
	main_port = 0;
	replica_sockets_index = 0;

	init_users();

	char *type = argv[1];

	if (COMPARE_EQUAL_STRING(type, "main"))
	{
		main_port = atoi(argv[2]);
		pthread_t main_process = start_all_main_services_starting_at_port(main_port);
		pthread_join(main_process, NULL);
	}
	else if (COMPARE_EQUAL_STRING(type, "replica"))
	{
		char *main_host = argv[2];
		main_port = atoi(argv[3]);
		int execution_status = start_as_replica_server(main_host, main_port);
		// se ocorreu uma falha ...
		if (execution_status < 0)
			return -1;
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
	execute_tcp_server_listener_callback_nonblock(port_replication, replication_server, replica_manager_disconnection);
	// start dropbox wait service
	return execute_tcp_server_listener_nonblock(port, client_intermediate_process);
}

void *client_intermediate_process(void *args)
{
	int client_sock = *(int *)args;
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

void *replicas_update_ips_list(void *args)
{
	char message_buffer[4096];
	int exit_status = 0;
	int sockfd = *(int *)args;

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

	while (true)
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

		if (read_return < 0 || write_return < 0 || errno != 0)
		{
			perror("Error sending update:");
			exit_status = -1;
			break;
		}
	}

	// remove socket from list
	replica_manager_disconnection(sockfd);

	pthread_exit((void *)&exit_status);
}

void *replication_server(void *args)
{
	// declare variables
	int sockfd = *(int *)args;

	// push replica socket to replicas sockets array
	SCOPELOCK(replica_socket_mutex, {
		replica_sockets[replica_sockets_index++] = sockfd;
	});

	// make replica wait
	while (true)
	{
		char message_from_replica[4096];
		int read_count = read_until_eos(sockfd, message_from_replica);
		if (read < 0)
		{
			// TODO: Error on reading, handle it
		}
	}
}

void *replica_manager_disconnection(int socket)
{
	if (last_replication_transaction_status == RUNNING)
	{
		last_replication_transaction_status = FAILED;
	}

	// Remove replica connection socket from replicas socket array
	SCOPELOCK(replica_socket_mutex, {
		int socket_index = -1;
		// find equals socket descriptor
		for (int i = 0; i < replica_sockets_index; i++)
		{
			if (replica_sockets[i] == socket)
			{
				// when found, moving all sockets -1 index
				for (int j = i; j < replica_sockets_index - 1; j++)
					replica_sockets[j] = replica_sockets[j + 1];
				replica_sockets_index--;
				break;
			}
		}
	});
}

bool start_replica_transaction(char *command, char *username, char *filename, char *modtime, int filesize)
{
	last_replication_transaction_status = RUNNING;

	SCOPELOCK(replica_socket_mutex, {
		SCOPELOCK(receive_ip_mutex, {

			// assumption that it will fail
			last_replication_transaction_status = FAILED;
			// verify it by checking the last socket index and number
			// of sockets replica + 1 if they are the same means that the
			// all done right. Failed otherwise.
			int socket_index;

			// send command and data for all replicas connected
			for (socket_index = 0; socket_index < replica_sockets_index; socket_index++)
			{
				// get socket reference
				int sockfd = replica_sockets[socket_index];

				// send command
				if (write_str_to_socket(sockfd, command) < 0)
					break;

				// send user
				if (write_str_to_socket(sockfd, username) < 0)
					break;

				// if its a file upload, send file information
				if (COMPARE_EQUAL_STRING(command, REPLICATE_FILE))
				{
					printf("sending file: %s\n", filename);
					// send file info data
					char file_info[4096] = "";
					package_file(filename, modtime, filesize, file_info);
					if (write_str_to_socket(sockfd, file_info) < 0)
						break;
				}
				else if (COMPARE_EQUAL_STRING(command, DELETE_FILE))
				{
					// send file name to delete
					if (write_str_to_socket(sockfd, filename) < 0)
						break;
				}
				else
				{
					printf("COMMAND \"%s\" NOT RECOGNIZED\n", command);
					last_replication_transaction_status = NOT_RUNNING;
					break;
				}

				// notify replica that it has started data section
				if (write_str_to_socket(sockfd, START_DATA_SECTION) < 0)
					break;
			}

			// if transmission data was sent to all replicas sockets, continue
			if (socket_index == replica_sockets_index)
				last_replication_transaction_status = RUNNING;
		});
	});

	return true;
}

bool send_file_to_replicas(char *filename, int filesize)
{
	if (last_replication_transaction_status == RUNNING)
	{
		// FILE* input_file = fopen(filename, "rb");
		// fseek(input_file, 0, SEEK_END);
		// size_t file_size = ftell(input_file);
		// fseek(input_file, 0, SEEK_SET);

		// char* file_content = TCALLOC(char, file_size);
		// fread(file_content, 1, file_size, input_file);

		SCOPELOCK(replica_socket_mutex, {
			SCOPELOCK(receive_ip_mutex, {

				// assumption that it will fail
				last_replication_transaction_status = FAILED;
				// verify it by checking the last socket index and number
				// of sockets replica + 1 if they are the same means that the
				// all done right. Failed otherwise.
				int socket_index;

				// send command and data for all replicas connected
				for (socket_index = 0; socket_index < replica_sockets_index; socket_index++)
				{
					// send a piece of the 
					int sockfd = replica_sockets[socket_index];
					if (write_file_to_socket(sockfd, filename, filesize) < 0)
						break;
					// if (write(sockfd, file_content, file_size) < 0)
					// 	break;
				}

				// if transmission data was sent to all replicas sockets, continue
				if (socket_index == replica_sockets_index)
					last_replication_transaction_status = RUNNING;
			});
		});
	}

	return true;
}

bool replica_delete_file(char *filename)
{
	if (last_replication_transaction_status == RUNNING)
	{

		SCOPELOCK(replica_socket_mutex, {
			SCOPELOCK(receive_ip_mutex, {

				// assumption that it will fail
				last_replication_transaction_status = FAILED;
				// verify it by checking the last socket index and number
				// of sockets replica + 1 if they are the same means that the
				// all done right. Failed otherwise.
				int socket_index;

				// send command and data for all replicas connected
				for (socket_index = 0; socket_index < replica_sockets_index; socket_index++)
				{
					// send a piece of the 
					int sockfd = replica_sockets[socket_index];
					if (write_str_to_socket(sockfd, filename) < 0)
						break;
				}

				// if transmission data was sent to all replicas sockets, continue
				if (socket_index == replica_sockets_index)
					last_replication_transaction_status = RUNNING;
			});
		});
	}

	return true;
}

bool commit_replica_transaction(char *command)
{
	char message_to_replicas[256];

	if (COMPARE_EQUAL_STRING(command, ROLLBACK))
	{
		strcpy(message_to_replicas, ROLLBACK);
	}
	else
	{
		if (last_replication_transaction_status == RUNNING)
			strcpy(message_to_replicas, END_DATA_SECTION);
		else if (last_replication_transaction_status == FAILED)
			strcpy(message_to_replicas, ROLLBACK);
	}

	printf("File sent last command: %s\n", message_to_replicas);

	SCOPELOCK(replica_socket_mutex, {
		SCOPELOCK(receive_ip_mutex, {

			// assumption that it will fail
			last_replication_transaction_status = FAILED;
			// verify it by checking the last socket index and number
			// of sockets replica + 1 if they are the same means that the
			// all done right. Failed otherwise.
			int socket_index;

			// send command and data for all replicas connected
			for (socket_index = 0; socket_index < replica_sockets_index; socket_index++)
				if (write_str_to_socket(replica_sockets[socket_index], message_to_replicas) < 0)
					break;

			// if transmission data was sent to all replicas sockets, continue
			if (socket_index == replica_sockets_index)
				last_replication_transaction_status = RUNNING;
		});
	});

	last_replication_transaction_status = NOT_RUNNING;
	return true;
}

void *time_server_client_process(void *sock_ptr)
{
	// declare variables
	int sockfd = *(int *)sock_ptr;
	char buffer[256];

	free(sock_ptr);

	// read current time, write to buffer,
	// send via socket and close it
	sprintf(buffer, "%lu", time(0));
	write_str_to_socket(sockfd, buffer);
	close(sockfd);
}
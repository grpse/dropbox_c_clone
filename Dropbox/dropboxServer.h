#ifndef DROPBOXSERVER_H
#define DROPBOXSERVER_H

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


#include "dropboxUtil.h"
#include "processmessages.h"


#define MAIN_PORT main_port
#define TIME_PORT (main_port + 1)
#define HEART_PORT (main_port + 2)
#define REPLICATION_PORT (main_port + 3)

void* client_intermediate_process(void* args);
void* replicas_update_ips_list(void* args);
void* replication_server(void* args);
void* replica_manager_disconnection(int socket);

int start_replica_transaction(char* command, char* username, char* filename, char* modtime, int filesize );
bool send_file_to_replicas(char *filename, int filesize);
bool replica_delete_file(char *filename);
int commit_replica_transaction(char* command);

void* time_server_client_process(void* sock_ptr);

pthread_t start_all_main_services_starting_at_port(int main_port);

#define wait_for_dropbox_client_connections(port) execute_tcp_server_listener_block((port), client_process)
#define wait_for_dropbox_client_connections_nonblock(port) execute_tcp_server_listener_nonblock((port), client_process)

#define get_my_ip(ip_buffer) get_ip_list((ip_buffer))
#define send_clients_ip_list(sockfd, clients_ip_list) write_str_to_socket((sockfd), (clients_ip_list))
#define send_replica_order(sockfd, order) write_int_to_socket((sockfd), (order))                                             
#define receive_my_replica_order(sockfd, order) read_int_from_socket((sockfd), (order))    
    
#endif /*DROPBOXSERVER_H*/
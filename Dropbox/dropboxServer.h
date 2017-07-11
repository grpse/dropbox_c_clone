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

struct ReplicasUpdateVerifyParams{
    char* main_host;
    int main_port;
    int sockfd;
    int my_order;
    pthread_t update_thread;
    char next_host[16];
};

void* client_intermediate_process(void* args);
int start_as_replica_server(char* main_host, int main_port);
void* replicas_update_ips_list(void* args);
void* receive_replica_files(void* args);

int start_replica_transaction(char* command, char* username, char* filename, char* modtime, int filesize );
int replica_file_get_copy_buffer(char* buffer, int size);
int commit_replica_transaction(char* command);

void* time_server_client_process(void* sock_ptr);
void* update_replicas_and_clients_ip_list(void* replicasUpdateVerifyParams);
void* verifying_disconnection_to_reconnect_or_turn_it_main_server(void* replicasUpdateVerifyParams);
int get_new_last_order(char* replicas_ip_list);

pthread_t start_all_main_services_starting_at_port(int main_port);

#define wait_for_dropbox_client_connections(port) execute_tcp_server_listener_block((port), client_process)
#define wait_for_dropbox_client_connections_nonblock(port) execute_tcp_server_listener_nonblock((port), client_process)

#define get_my_ip(ip_buffer) get_ip_list((ip_buffer))
#define send_clients_ip_list(sockfd, clients_ip_list) write_str_to_socket((sockfd), (clients_ip_list))
#define send_replica_order(sockfd, order) write_int_to_socket((sockfd), (order))                                             
#define receive_my_replica_order(sockfd, order) read_int_from_socket((sockfd), (order))    
    
#endif /*DROPBOXSERVER_H*/
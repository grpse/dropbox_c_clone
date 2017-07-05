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

int start_as_replica_server(char* main_host, int main_port);
int wait_for_dropbox_client_connections(int wait_port);
int create_tcp_server(int port);
void* time_server(void* port_ptr);
void* time_server_client_process(void* sock_ptr);
void get_ip_list(char** ip_list, int* ip_list_count);

#endif /*DROPBOXSERVER_H*/
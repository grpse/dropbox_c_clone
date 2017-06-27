#ifndef DROPBOXSERVER_H
#define DROPBOXSERVER_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

#include "dropboxUtil.h"
#include "processmessages.h"

int create_tcp_server(int port);
void* time_server(void* port_ptr);
void* time_server_client_process(void* sock_ptr);

#endif /*DROPBOXSERVER_H*/
#ifndef DROPBOXRM_H
#define DROPBOXRM_H

#include "dropboxServer.h"
#include "packager.h"

struct ReplicasUpdateVerifyParams{
    char* main_host;
    int main_port;
    int sockfd;
    int my_order;
    pthread_t update_thread;
    char next_host[16];
};

int start_as_replica_server(char* main_host, int main_port);
void send_all_clients_my_ip(char* my_ip);
void* receive_replica_files(void* args);
void* update_replicas_and_clients_ip_list(void* replicasUpdateVerifyParams);
void* verifying_disconnection_to_reconnect_or_turn_it_main_server(void* replicasUpdateVerifyParams);
int get_new_last_order(char* replicas_ip_list);

#endif /*DROPBOXRM_H*/
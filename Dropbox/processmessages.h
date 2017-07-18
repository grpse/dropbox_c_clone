#ifndef PROCESSMESSAGES_H
#define PROCESSMESSAGES_H

#include <time.h>

void init_users();

void* client_process(void* clsock_ptr);

#endif /*PROCESSMESSAGES_H*/

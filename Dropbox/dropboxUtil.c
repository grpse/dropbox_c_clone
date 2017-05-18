#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

int read_n_from_socket(int n, int sock, char *buffer){
	int i = 0;
	int r = 0;

	while(i < n){
		r = read(sock, &buffer[i], 1);
		if(r < 0){
			return -1;
		}
		i+=r;
	}
}

int read_until_eos(int sock, char * buffer){
	int i = 0;
	int r = 0;

	do{
		r = read(sock, &buffer[i], 1);
		if (r < 0)
			return -1;
		i += r;
	}while(buffer[i-r]);

	return i;
}

int write_str_to_socket(int sock, char * str){
	int n;

	n = write(sock, str, strlen(str) + 1);
	return n;
}

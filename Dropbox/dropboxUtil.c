#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
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

int read_and_save_to_file(int sock, char * filename, int fsize){
	int f = -1;
	if ((f = creat(filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0)
		return -1;

	int k = 0, r;
	char c;
	while(k < fsize){
		r = read(sock, &c, 1);
		if (r < 0)
			break;

		//printf("%c", c);
		r = write(f, &c, 1);
		if (r < 0){
			puts("Error writing file");
			close(f);
			return -1;
		}
		k += r;
	}
	close(f);
}

int write_file_to_socket(int sock, char * filename, int fsize){
	int f = open(filename, O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if(f < 0){
		return -1;
	}
	int k = 0, r;
	char c;
	while(k < fsize){
		r = read(f, &c, 1);
		if (r<0)
			break;
		r = write(sock, &c, 1);
		if (r<0)
			break;
		k += r;
	}
	close(f);
}

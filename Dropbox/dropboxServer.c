#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

//#include "packager.h"
#include "dropboxUtil.h"
#include "processmessages.h"


int main(int argc, char * argv[]){
	int sockfd, newsockfd, n;
	socklen_t clilen;

	if (argc < 2) {
		printf("usage: %s <port>\n", argv[0]);
	}
	// char buffer[256];
	int port = atoi(argv[1]);


	init_users();

	struct sockaddr_in serv_addr, cli_addr;

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
  	printf("ERROR opening socket\n");
		exit(1);
	}
	
	int enable = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
		perror("setsockopt(SO_REUSEADDR) failed");

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	bzero(&(serv_addr.sin_zero), 8);

	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
		printf("ERROR on binding\n");
		exit(1);
	}
	listen(sockfd, 10);

	clilen = sizeof(struct sockaddr_in);

	while(1){
		if ((newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen)) == -1) {
			printf("ERROR on accept");
			exit(1);
		}
		pthread_t thc;
		int * clsock = (int *)malloc(sizeof(int));
		*clsock = newsockfd;
		pthread_create(&thc, NULL, client_process, clsock);
	}
	close(sockfd);
	return 0;
}

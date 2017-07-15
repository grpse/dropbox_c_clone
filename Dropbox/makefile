all: server client

server:
	gcc -std=gnu99 -o bin/dropboxServer -pthread -O3 dropboxServer.c dropboxRM.c client.c dropboxUtil.c packager.c processmessages.c

client:
	gcc -std=gnu99 -o bin/dropboxClient -pthread -O3 dropboxClient.c dropboxUtil.c packager.c

clean:
	rm bin/dropboxClient bin/dropboxServer

server_debug:
	gcc -std=gnu99 -o bin/dropboxServer -pthread -O3 dropboxServer.c client.c dropboxUtil.c packager.c processmessages.c -ggdb
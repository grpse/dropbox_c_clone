all: server client

server:
	gcc -o bin/dropboxServer -pthread -O3 dropboxServer.c client.c dropboxUtil.c packager.c processmessages.c

client:
	gcc -o bin/dropboxClient -pthread -O3 dropboxClient.c dropboxUtil.c packager.c

clean:
	rm bin/dropboxClient bin/dropboxServer

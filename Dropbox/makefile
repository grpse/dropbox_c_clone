all: server client

server:
	gcc -o dropboxServer -pthread -O3 dropboxServer.c client.c dropboxUtil.c packager.c processmessages.c

client:
	gcc -o dropboxClient -O3 dropboxClient.c dropboxUtil.c packager.c

clean:
	rm dropboxClient dropboxServer

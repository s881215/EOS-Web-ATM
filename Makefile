CC=gcc
CFLAGS= -g -Wall
CFLAGS2= -g -Wall
ifdef USE_SEM
CFLAGS2 += -DUSE_SEM
endif

.PHONY: all clean

all:client server

client: client.c
	$(CC) $(CFLAGS) -o client client.c
server: server.c
	$(CC) $(CFLAGS) -o server server.c

clean:
	rm -f client
	rm -f server

CC     = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu17
LFLAGS =

.PHONY: all clean

TARGETS = server client

all: $(TARGETS)

client: client.o err.o common.o
server: server.o err.o common.o

err.o: err.c err.h
common.o: common.c common.h
client.o: client.c err.h common.h
server.o: server.c err.h common.h

clean:
	rm -f $(TARGETS) *.o *~
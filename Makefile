CC     = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu17
LFLAGS =

.PHONY: all clean

TARGETS = kierki-serwer kierki-klient

all: $(TARGETS)

kierki-klient: kierki-klient.o err.o common.o
kierki-serwer: kierki-serwer.o err.o common.o

err.o: err.c err.h
common.o: common.c common.h
kierki-klient.o: kierki-klient.c err.h common.h
kierki-serwer.o: kierki-serwer.c err.h common.h

clean:
	rm -f $(TARGETS) *.o *~
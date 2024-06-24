CC=gcc
CFLAGS=-std=c11 -D_DEFAULT_SOURCE -Wall -pthread

all: PideShop HungryVeryMuch

PideShop: server.o
	$(CC) $(CFLAGS) -o PideShop server.o -lm

HungryVeryMuch: client.o
	$(CC) $(CFLAGS) -o HungryVeryMuch client.o 

server.o: server.c
	$(CC) $(CFLAGS) -c server.c

client.o: client.c
	$(CC) $(CFLAGS) -c client.c

.PHONY: clean
clean:
	rm -f *.o *.out *.log PideShop HungryVeryMuch


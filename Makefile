UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
    CFLAGS=-Wall -pthread
    LDFLAGS=-pthread -lrt
    CC=gcc
endif

ifeq ($(UNAME_S),FreeBSD)
    CFLAGS=-Wall -pthread
    LDFLAGS=-pthread -lrt
    CC=cc
endif

.PHONY: all
all: mq-test msg-test

mq-test: mq-test.o
	$(CC) -o $@ $^ $(LDFLAGS)

mq-test.o: mq-test.c
	$(CC) -c $< $(CFLAGS)

msg-test: msg-test.o
	$(CC) -o $@ $^ $(LDFLAGS)

msg-test.o: msg-test.c
	$(CC) -c $< $(CFLAGS)

clean:
	rm -f *.o mq-test msg-test

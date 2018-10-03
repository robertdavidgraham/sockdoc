
CC = gcc
CFLAGS = -Wall

TCPSRV = bin/tcp-srv-one bin/tcp-srv-fork bin/tcp-srv-poll
TCPCLIENT = bin/tcp-client bin/tcp-send-fail

bin/% : src/%.c
	$(CC) $(CFLAGS) -o $@ $<

all: $(TCPSRV) $(TCPCLIENT)


clean:
	rm bin/*




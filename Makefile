
CC = gcc
CFLAGS = -Wall -g

TCPSRV = bin/tcp-srv-one bin/tcp-srv-fork bin/tcp-srv-poll bin/tcp-srv-sigpipe
TCPCLIENT = bin/tcp-client bin/tcp-send-fail \
	bin/tcp-client-bind

bin/% : src/%.c
	$(CC) $(CFLAGS) -o $@ $<

all: $(TCPSRV) $(TCPCLIENT) bin/some-tests


clean:
	rm bin/*




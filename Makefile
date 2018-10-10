
CC = gcc
CFLAGS = -Wall

TCPSRV = bin/tcp-srv-one bin/tcp-srv-fork bin/tcp-srv-poll bin/tcp-srv-sigpipe
TCPCLIENT = bin/tcp-client bin/tcp-send-fail bin/tcp-client-poll \
	bin/tcp-client-bind

bin/% : src/%.c
	$(CC) $(CFLAGS) -o $@ $<

all: $(TCPSRV) $(TCPCLIENT)


clean:
	rm bin/*




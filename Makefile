
CC = gcc
WARNINGS = -Wall -Wformat -Wformat-security -Werror=format-security
SECFLAGS = -fstack-protector -fPIE  -D_FORTIFY_SOURCE=2
CFLAGS = -Wall -g -O2 $(WARNINGS) $(SECFLAGS)

TCPSRV = bin/tcp-srv-echo bin/tcp-srv-fork bin/tcp-srv-poll bin/tcp-srv-sigpipe
TCPCLIENT = bin/tcp-client bin/tcp-send-fail \
	bin/tcp-client-bind
TESTS = bin/some-tests bin/list-addr bin/test-eintr bin/test-aslr

bin/% : src/%.c
	$(CC) $(CFLAGS) -o $@ $<

all: $(TCPSRV) $(TCPCLIENT) $(TESTS)


clean:
	rm bin/*




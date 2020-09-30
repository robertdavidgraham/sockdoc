CC = gcc
CFLAGS = -g -ggdb -Wall -Wpedantic -Wextra \
	-D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE \
	-Wformat -Wformat-security 

TARGETS = bin/dns-unittest bin/sha512-unittest bin/chacha20-unittest bin/secmem-unittest \
	bin/resolv

all: $(TARGETS)

bin/sha512-unittest: util-sha512.c util-sha512.h
	@echo   $@
	@$(CC) -DSHA512STANDALONE $(CFLAGS) $< -o $@

bin/chacha20-unittest: util-chacha20.c util-chacha20.h
	@echo $@
	@$(CC) -DCHACHA20STANDALONE $(CFLAGS) $< -o $@

bin/secmem-unittest: util-secmem.c util-secmem.h
	@echo $@
	@$(CC) -DSECMEMSTANDALONE $(CFLAGS) $< -o $@

bin/dns-unittest: dns-unittest.c dns-parse.c dns-format.c dns-parse.h dns-format.h
	@echo $@
	$(CC) $(CLFAGS) -ftest-coverage --coverage dns-unittest.c dns-parse.c dns-format.c  -o $@

bin/resolv: dns-resolv.c dns-parse.c dns-format.c dns-parse.h dns-format.h
	@echo $@
	@$(CC) $(CLFAGS) -lresolv dns-resolv.c dns-parse.c dns-format.c -lresolv -o $@

test: bin/sha512-unittest bin/chacha20-unittest bin/secmem-unittest bin/dns-unittest
	@cd bin; ./sha512-unittest --test
	@cd bin; ./chacha20-unittest --test
	@cd bin; ./secmem-unittest --test
	@cd bin; ./dns-unittest
	

coverage: bin/dns-unittest
	@./bin/dns-unittest
	@gcov dns-parse.c

clean: 
	@rm -f $(TARGETS) *.gcda




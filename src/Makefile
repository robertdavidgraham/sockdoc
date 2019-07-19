CC = gcc
CFLAGS = -Wall -Wpedantic -Wextra -fsanitize=undefined \
	-D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE \
	-Wformat -Wformat-security 

TARGETS = sha512 chacha20

sha512: util-sha512.c util-sha512.h
	$(CC) -DSHA512STANDALONE $(CFLAGS) $< -o $@

chacha20: util-chacha20.c util-chacha20.h
	$(CC) -DCHACHA20STANDALONE $(CFLAGS) $< -o $@

secmem: util-secmem.c util-secmem.h
	$(CC) -DSECMEMSTANDALONE $(CFLAGS) $< -o $@

test: sha512 chacha20 secmem
	@./sha512 --test
	@./chacha20 --test
	@./secmem --test

clean: 
	@rm -f $(TARGETS)

all: sha512
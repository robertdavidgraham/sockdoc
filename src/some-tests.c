#include <errno.h> /* errno */
#include <stdio.h>
#include <stdlib.h> /* malloc() */
#include <string.h> /* strerror() */
#include <time.h>

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h> /* fcntl(), F_GETFL, O_NONBLOCK */

int sock_debug = 1;

/**
 * Configures the socket for non-blocking status
 */
int sock_set_nonblocking(int fd)
{
	int flag;
	flag = fcntl(fd, F_GETFL, 0);
	flag |= O_NONBLOCK;
	flag = fcntl(fd, F_SETFL,  flag);
	if (flag == -1)
		return -1;
	else
		return 0;
}

/**
 * Tests if a socket is set in non-blocking mode
 */
int sock_is_nonblocking(int fd)
{
	return (fcntl(fd, F_GETFL, 0) & O_NONBLOCK) == O_NONBLOCK;
}

/**
 * This tests whether sockets create by 'accept()' will inherit the 
 * non-blocking status of the listening socket. This creates a server
 * socket set to non-blocking, then creates a client socket to connect
 * to the server, then tests the blocking status of that third socket.
 */
int accept_inherits_nonblocking(void)
{
	int err;
	int fd = -1;
	int fd_out = -1;
	int fd_in = -1;
	struct addrinfo *ai;
	struct addrinfo hints = {0};
	struct sockaddr_storage sa = {0};
	socklen_t sa_len = sizeof(sa);

	/* Create a passive/half-open socket for listening */
    hints.ai_flags = AI_PASSIVE;
	err = getaddrinfo(0, "55555", &hints, &ai);
	if (err) {
		fprintf(stderr, "inherit:getaddrinfo(): %s\n", gai_strerror(err));
		goto cleanup;;
	}
    fd = socket(ai->ai_family, SOCK_STREAM, 0);
	sock_set_nonblocking(fd);
    err = bind(fd, ai->ai_addr, ai->ai_addrlen);
    err = listen(fd, 10);
	err = getsockname(fd, (struct sockaddr *)&sa, &sa_len);

	/* Create a client socket for the outgoing connection to this server */
	fd_out = socket(sa.ss_family, SOCK_STREAM, 0);
	err = connect(fd_out, (struct sockaddr *)&sa, sa_len);
	if (err != 0) {
		fprintf(stderr, "inherit:connect(): %s\n", strerror(errno));
		return -1;
	}

	/* Accept the incoming client connection */
again:
	fd_in = accept(fd, 0, 0);
	if (fd_in == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
		goto again;
	if (fd_in == -1) {
		fprintf(stderr, "inherit:accept(): %s\n", strerror(errno));
		return -1;
	}

	fprintf(stderr, "inherit blocking = %s\n", sock_is_nonblocking(fd_in)?"yes":"no");

cleanup:
	if (fd != -1)
		close(fd);
	if (fd_out != -1)
		close(fd_out);
	if (fd_in != -1)
		close(fd_in);

	return 0;
}

int main(void)
{
	/* The byte-order of the internal machine should be irrelevent, but since
	 * most people discuss it, we describe it here */
	if (*(int*)"\x01\x02\x03\x04" == 0x01020304)
		printf("byte-order = big-endian\n");
	else if (*(int*)"\x01\x02\x03\x04" == 0x04030201)
		printf("byte-order = little-endian\n");
	else
		printf("byte-order = unknown\n");

	/* This tells us if we are running on a 32-bit or 64-bit system */
	printf("sizeof(size_t) = %u-bits\n", 8 * (unsigned)sizeof(size_t));

	/* virtually every system, from the 16-bit era through the 64-bit, have
	 * defined this to be 32-bits. Some 64-bit platforms make this 64-bit,
	 * however */
	printf("sizeof(int) = %u-bits\n", 8 * (unsigned)sizeof(int));

	/* This is defined to be 64-bits on most 64-bit platforms, though
	 * Win64 keeps this at 32-bits */	
	printf("sizeof(long) = %u-bits\n", 8 * (unsigned)sizeof(long));

	/* This should be 64-bits everywhere */
	printf("sizeof(long long) = %u-bits\n", 8 * (unsigned)sizeof(long long));

	/* On 64-bit systems, this should be 64-bits. On Windows since Visual C++ 2005,
	 * this is 64-bits even on 32-bit platforms. On most 32-bit platforms, this
	 * is 32-bits, and wraps in the year 2038. */
	printf("sizeof(time_t) = %u-bits\n", 8 * (unsigned)sizeof(time_t));

	/* An example formatting timestamps. Microsoft gives a warning suggesting
	 * gmttime_s() instead. */
	{
		char buffer[80];
        struct tm *x;
        time_t now = time(0);
        x = gmtime(&now);
        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", x);
		printf("timestamp = %s\n", buffer);
	}
	
	accept_inherits_nonblocking();

	return 0;
}




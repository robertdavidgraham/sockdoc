/* tcp-srv-poll
 Simple example of TCP server written with poll.
 This is an 'echo' server that echoes back whatever it receives.
 Example usage:
    tcp-srv-one 7777
 This will listen on port 7777, accept one connection at a time,
 and echo back whatever it receives on the connection.
 */
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netdb.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

struct connection_t {
  /* Instead of calling `getpeername()` every time we want to log a message
   * about this connection, we remember it here in this structure */
  struct sockaddr_in6 sa;
  socklen_t sa_addrlen;

  /* Instead of formatting the address to text every time we want to log
   * a message about this connection, we just format it once. */
  char peeraddr[64];
  char peerport[8];

  /* The current length of our buffer */
  ptrdiff_t len;

  /* Instead of immediately using `send()` to echo back what we got on the
   * `recv()`, we'll buffer the data in user-mode. This deals with the
   * problem that under load, the kernel may run out of buffer space,
   * and there may be no way to `send()` data without causing this
   * server to block. In real-world use of `poll()`, this is something
   * almost every programmer will do, buffering data in user-mode. */
  char buf[512];
};

struct poller_t {
  struct connection_t *connections;
  struct pollfd *list;
  size_t count;
  size_t max;
};

/** We are calling our subsystem a "poller". This contains the array of
 * descriptors that we manage for the `poll()` system call, as well
 * as an array of descriptions about each connection.
 *
 * The first item in the array isn't a connection, but the half-open
 * server that we'll use to receive connections. */
struct poller_t *poller_create(int fd) {
  struct poller_t *poller;
  poller = malloc(sizeof(*poller));

  /* Start with 2 entries, one for the server, and one empty for incoming */
  poller->max = 2;
  poller->list = malloc(poller->max * sizeof(*poller->list));
  poller->list[0].fd = fd;
  poller->list[0].events = POLLIN; /* incoming connection events */
  poller->count = 1;
  poller->connections = malloc(poller->max * sizeof(*poller->connections));

  return poller;
}

/**
 * Right after calling`accept()` for an incomign connection, we use this
 * function to add that connection to our arrays. We have one array
 * for tracking the descriptor (`fd`) and associated poll events. We have
 * another array that tracks a buffer for outoing data for each `fd`.
 * The `pollfd` array is part of the Scokets API, so we can't add custom
 * fieldds to it, hence the second array.
 */
void poller_add(struct poller_t *poller, int fd, struct sockaddr_in6 *sa,
                socklen_t sa_addrlen) {
  struct connection_t *c;
  int err;

  /* add to the poll() list, set for reading */
  poller->list[poller->count].fd = fd;
  poller->list[poller->count].events = POLLIN;
  poller->list[poller->count].revents = 0;

  /* add per-connection buffer and logging info */
  c = &poller->connections[poller->count];
  c->len = 0; /* init buffer */
  c->sa_addrlen = sa_addrlen;
  memcpy(&c->sa, sa, sa_addrlen);

  /* log the address of remote connection */
  err = getnameinfo((struct sockaddr *)&c->sa, c->sa_addrlen, c->peeraddr,
                    sizeof(c->peeraddr), c->peerport, sizeof(c->peerport),
                    NI_NUMERICHOST | NI_NUMERICSERV);
  if (err) {
    fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
    memcpy(c->peeraddr, "err", 4);
    memcpy(c->peerport, "err", 4);
  } else {
    fprintf(stderr, "[+] connect() from [%s]:%s\n", c->peeraddr, c->peerport);
  }

  /* grow the lists by 1, if needed*/
  poller->count++;
  if (poller->count >= poller->max) {
    poller->max++;
    poller->list = realloc(poller->list, poller->max * sizeof(*poller->list));
    poller->connections = realloc(poller->connections,
                                  poller->max * sizeof(*poller->connections));
  }
}

/**
 * Removes a connection.
 * The array has to be dense, so we can't leave a hole in the middle.
 * Therefore, we move whatever entry is at the end of our arrays to the
 * spot just vacated.
 */
void poller_remove_at(struct poller_t *poller, size_t i) {
  size_t end;

  /* close the socket if it's still open */
  if (poller->list[i].fd > 0) {
    close(poller->list[i].fd);
    poller->list[i].fd = -1;
  }

  /* For efficiency, replace this entry with the one at the end of the list */
  end = poller->count - 1;
  if (end > i) {
    memcpy(&poller->list[i], &poller->list[end], sizeof(poller->list[0]));
    memcpy(&poller->connections[i], &poller->connections[end],
           sizeof(poller->connections[0]));
  }
  poller->count--;
}

/**
 * This cleans up our "poller" subsystem.
 */
void poller_destroy(struct poller_t *poller) {
  while (poller->count)
    poller_remove_at(poller, poller->count - 1);

  free(poller->list);
  free(poller->connections);
}

int main(int argc, char *argv[]) {
  struct addrinfo *ai = NULL;
  struct addrinfo hints = {0};
  int err;
  int fd = -1;
  char hostaddr[NI_MAXHOST];
  char hostport[NI_MAXSERV];
  struct poller_t *poller = NULL;

  /* Ignore the send() problem */
  signal(SIGPIPE, SIG_IGN);

  if (argc < 2 || 3 < argc) {
    fprintf(stderr, "[-] usage: tcp-srv-one <port> [address]\n");
    return -1;
  }

  /* Get an address structure for the port */
  hints.ai_flags = AI_PASSIVE;
  err = getaddrinfo((argc == 3) ? argv[2] : 0, /* local address*/
                    argv[1],                   /* local port number */
                    &hints,                    /* hints */
                    &ai);                      /* result */
  if (err) {
    fprintf(stderr, "[-] getaddrinfo(): %s\n", gai_strerror(err));
    return -1;
  }

  /* ..and retrieve back again which addresses were assigned. Normally,
   * this will be [::] meaning we are accepting incoming connections
   * on all our IP addresses, even IPv4 addresses */
  err =
      getnameinfo(ai->ai_addr, ai->ai_addrlen, hostaddr, sizeof(hostaddr),
                  hostport, sizeof(hostport), NI_NUMERICHOST | NI_NUMERICSERV);
  if (err) {
    fprintf(stderr, "[-] getnameinfo(): %s\n", gai_strerror(err));
    goto cleanup;
  }

  /* Create a file handle for the half-open server  */
  fd = socket(ai->ai_family, SOCK_STREAM, 0);
  if (fd == -1) {
    fprintf(stderr, "[-] socket(): %s\n", strerror(errno));
    goto cleanup;
  }

  /* Allow multiple processes to share this IP address */
  {
    int yes = 1;

    err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (err) {
      fprintf(stderr, "[-] SO_REUSEADDR([%s]:%s): %s\n", hostaddr, hostport,
              strerror(errno));
      goto cleanup;
    }
  }

  /* Allow multiple processes to share this port, if it's available
   * in this operating-ssytem */
#if defined(SO_REUSEPORT)
  {
    int yes = 1;

    err = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
    if (err) {
      fprintf(stderr, "[-] SO_REUSEPORT([%s]:%s): %s\n", hostaddr, hostport,
              strerror(errno));
      goto cleanup;
    }
  }
#endif

  /* Tell it to use the local port number (and optionally, address).
   * These are specified on the command-line */
  err = bind(fd, ai->ai_addr, ai->ai_addrlen);
  if (err) {
    fprintf(stderr, "[-] bind([%s]:%s): %s\n", hostaddr, hostport,
            strerror(errno));
    goto cleanup;
  }

  /* Configure the socket for listening (i.e. accept incoming connections)
   */
  err = listen(fd, 10);
  if (err) {
    fprintf(stderr, "[-] listen([%s]:%s): %s\n", hostaddr, hostport,
            strerror(errno));
    goto cleanup;
  } else
    fprintf(stderr, "[+] listening on [%s]:%s\n", hostaddr, hostport);

  /* create an instance of our polling object */
  poller = poller_create(fd);
  fd = -1;

  /* dispatch loop */
  while (poller->count) {
    int timeout = 100; /* 100 milliseconds */
    size_t i;

    /* wait for incoming event on any connection */
    err = poll(poller->list, poller->count, timeout);
    if (err == -1) {
      /* fatal program error, shouldn't be possible */
      fprintf(stderr, "[-] poll(): %s\n", strerror(errno));
      break;
    } else if (err == 0) {
      /* timeout happened, nothing was recevied */
      continue;
    }

    /* accept incoming connections, if any */
    if (poller->list[0].revents) {
      struct sockaddr_in6 sa;
      socklen_t sa_addrlen = sizeof(sa);

      /* accept a new connection */
      fd = accept(poller->list[0].fd, (struct sockaddr *)&sa, &sa_addrlen);
      if (fd == -1) {
        fprintf(stderr, "[-] accept([%s]:%s): %s\n", hostaddr, hostport,
                strerror(errno));
        switch (errno) {
        case EMFILE:
          fprintf(stderr, "[-] files=%d, use 'ulimit -n <n>' to raise\n",
                  (int)poller->count);
          break;
        }
        continue;
      }

      /* add the new connection to the poller */
      poller_add(poller, fd, &sa, sa_addrlen);
    }

    /* handle all the TCP connections */
    for (i = 1; i < poller->count; i++) {
      struct connection_t *c = &poller->connections[i];
      if (poller->list[i].revents == 0) {
        /* no events for this socket */
        continue;
      } else if ((poller->list[i].revents & POLLHUP) != 0) {
        /* other side hungup (i.e. sent FIN, closed socket) */
        fprintf(stderr, "[+] close([%s]:%s): connection closed gracefully\n",
                c->peeraddr, c->peerport);
        poller_remove_at(poller, i--);
      } else if ((poller->list[i].revents & POLLERR) != 0) {
        /* error, probably RST sent by other side, but to be sure,
         * get the error associated with the socket */
        int opt;
        socklen_t opt_len = sizeof(opt);
        err = getsockopt(poller->list[i].fd, SOL_SOCKET, SO_ERROR, &opt,
                         &opt_len);
        if (err) {
          /* should never happen*/
          fprintf(stderr, "[-] getsockopt([%s]:%s): %s\n", c->peeraddr,
                  c->peerport, strerror(errno));
        } else {
          fprintf(stderr, "[-] recv([%s]:%s): %s\n", c->peeraddr, c->peerport,
                  strerror(opt));
        }
        poller_remove_at(poller, i--);
      } else if ((poller->list[i].revents & POLLIN) != 0) {
        /* Data is ready to receive */
        c->len = recv(poller->list[i].fd, c->buf, sizeof(c->buf), 0);
        if (c->len == 0) {
          /* Shouldn't be possible, should've got POLLHUP instead */
          fprintf(stderr, "[-] RECV([%s]:%s): %s\n", c->peeraddr, c->peerport,
                  "CONNECTION CLOSED");
          poller_remove_at(poller, i--);
        } else if (c->len < 0) {
          fprintf(stderr, "[-] RECV([%s]:%s): %s\n", c->peeraddr, c->peerport,
                  strerror(errno));
          poller_remove_at(poller, i--);
        } else {
          /* change poll() entry to transmit instead of receive */
          poller->list[i].events = POLLOUT;
        }
      } else if ((poller->list[i].revents & POLLOUT) != 0) {
        /* We are ready to transmit data */
        ptrdiff_t bytes_sent;
        bytes_sent = send(poller->list[i].fd, c->buf, c->len, 0);
        if (bytes_sent < 0) {
          /* might've reset connection between poll() and send() */
          fprintf(stderr, "[-] SEND([%s]:%s): %s\n", c->peeraddr, c->peerport,
                  strerror(errno));
          poller_remove_at(poller, i);
        } else if (bytes_sent < c->len) {
          /* hit the send() incomplete issue */
          fprintf(stderr, "[+] SEND([%s]:%s): %s\n", c->peeraddr, c->peerport,
                  "out of buffer");
          memmove(c->buf, c->buf + bytes_sent, c->len - bytes_sent);
          c->len -= bytes_sent;
          poller->list[i].events = POLLOUT;
        } else {
          /* all the bytes have been sent, so go back to reading */
          poller->list[i].events = POLLIN;
        }
      } else {
        fprintf(stderr, "[-] poll([%s]:%s): unknown event[%d] 0x%x\n",
                c->peeraddr, c->peerport, (int)i, poller->list[i].revents);
        poller_remove_at(poller, i--);
        exit(1);
      }
    } /* end handling connections */
  }   /* end dispatch loop */

cleanup:
  /* Cleans up and exits. In practice, we'll never reach this because
   * the code loops indefinitely until the user hits <ctrl-c> */
  if (fd > 0)
    close(fd);
  if (ai)
    freeaddrinfo(ai);
  if (poller)
    poller_destroy(poller);
  return 0;
}

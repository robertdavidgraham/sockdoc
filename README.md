sockdoc
=======

Examples for programming in C with the Sockets networking API.
In particular, these programs are intended to demonstarte issues
missing from textbooks.


Servers
===

`tcp-send-fail`
---

This program demonstrates forcing the `send()` function to transmit
too much data, filling up the buffer on the client side. It will
eventually hit a point where it returns indicating it hasn't sent
all the data.

The program stops once it hits this condition.

`tcp-srv-sigpipe`
---

This is just the same as the `tcp-srv-one` sample program, but it's
disabled the SIGPIPE line of code, so that these signals can be
generated.

Use the `tcp-send-fail` program to generate the fail condition. While
that client program was designed to test a different condition, it
will also close the TCP connection abruptly, causing the otherwise
rare condition that leads to SIGPIPE.

`tcp-srv-one`
---

This is just a typical example of a TCP server handling one connection
at a time, as to contrast against the other examples. Obviously
writing servers this way is a bad idea.

`tcp-srv-fork`
---

This is a typical example of a TCP server forking to handle
connections. It's not a terribly good way to handle connections,
but at least it's better than `tcp-srv-one`. On the other hand,
it's worse than `tcp-srv-thread`, `tcp-srv-poll`, and of course,
`tcp-srv-epoll`. The purpose of this program is to have somethin
to benchmark the various options against.


`tcp-srv-poll`
---

This is the basic asynchronous server design with a dispatch loop.
We then build on top of this with `tcp-srv-fiber` and `tcp-srv-epoll`.

This is a benchmark target, to contrast the performance of this 
server vs. the alternatives.

Clients
===

`tcp-client`
---

Typical client program that makes a single connection and gets a response.

`tcp-send-fail`
---

Similar to the client program above, except that it tries to shovel as
many bytes as possible across the TCP connection in order to overload
the buffers. It does this in order to demonstrate how the `send()`
function will eventually return without having sent all the requested
data.



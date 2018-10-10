tcp-client-bind
===

Demonstrates using `bind()` to assign a local port (and/or IP address) to
a TCP connection before connecting to remote port/address. It binds twice
deliberately in order to generate an error on the second attempt.

Command line:

    tcp-client-bind <remote-address> <remote-port> <local-port> [local-address]

where:

* remote-address - IPv4/IPv6/name of remote system to connect to
* remote-port - port (number between 1-65535) of remote system to connect to
* local-port - port on the local system to use for this side of the connection
* local-address - (optional) IPv4/IPv6 address on local system to use for this side of the connection


Code walkthrough
---

If a local address is specified, we need to use `getaddrinfo(local)` in order to
figure out the address family (IPv4 or IPv6). We need the address family
ahead of time so that when we call `getaddrinfo(remote)` we can pass in the 
address family, so that a DNS lookup will return the correct family. For example,
if we use a local address of `192.168.1.10`, then we want to passing `AF_INET` as
the address family when doing a lookup for `www.google.com`. Otherwise, the DNS
lookup might return an IPv6 address, and we can't connect from a local IPv4 address
to a remote IPv6 address. We could deal with this other ways, such as getting
the list of remote addresses, and then looping through them to figure out which
matches the local address family, but I chose this method. Note that except
for the address family, we throw away the results here and fetch them again
later.

    if (cfg.localname) {
        err = getaddrinfo(cfg.localname, cfg.localport, 0, &local);
        ...
        cfg.ai_family = local->ai_family;
        freeaddrinfo(local);
        ...
    }

Now for creating the socket, we care about the address family. If no local
address was specified, then it's going to be `AF_UNSPEC`, which means at
this moment, the stack doesn't know whether it's going to be IPv4 or IPv6.
Otherwise, it'll match the address family we fetched above for the *local*
address.

    fd = socket(target->ai_family, SOCK_STREAM, 0);

Like many examples in this guide, we set `SO_REUSEADDR`. Whereas the other
cases do this to deal with the TIME_WAIT issue, we do it here because
if there's already a TCP connection from a local port, we can't otherwise
bind to that port. We have to set this option to bind a socket to that
port.

In other words, if we want to use port `1234` to connect to both http://google.com:80
and http://yahoo.com:80, then we need to use this option. Both TCP connections
will have the same source port (`1234`), and the same source IP address, and in the same
destination port (`80`), but different destination IP addresses, so the
connections will be distinct from each other.

    err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

Now we need to get the local IP address for real, using the `AI_PASSIVE`
flag in the hints field. This informs the stack that this address is
for use on the local side. The only reason it cares is that if the
local address is empty, it returns an address of zero (`0.0.0.0` or `::`)
to indicate bind to any available local address. Otherwise, without
the "passive" flag, it would return a localhost adddress (`127.0.0.1' or `::1`).

    hints.ai_family = target->ai_family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    err = getaddrinfo(cfg.localname, cfg.localport, &hints, &local);

Now we need to call `bind()` on the local address. It's at this point that
it will allocate which local address and port it chooses. Remember that if
the port is 0, then the operating system will choose an ephemeral port.
And, if the IP address is zero (`0.0.0.0` or `::`) it will choose an
appropriate IP address from the default interface.

    err = bind(fd, local->ai_addr, local->ai_addrlen);

Now we call the `connect()` function. This should work on our first attempt,
then fail with an error on the second attempt, because we've hard-coded
both ports between the two addresses, and you cannot have two connections
that have the same ports and addresses.

    err = connect(fd, target->ai_addr, target->ai_addrlen);

After we do the connection we use the `getsockname()` function in order
to get the local address/port that was chosen.

    err = getsockname(fd, (struct sockaddr *)&sa, &sa_len);


Running the program
---

Here's what running this against a remote website looks like:

    $ bin/tcp-client-bind google.com 80 12345
    [+] local address family = AF_UNSPEC
    [+] getaddrinfo(): returned 14 addresses
    [+] target = [2607:f8b0:400d:c0d::8b]:80
    [+] local address = [::]:12345
    [+] connect(): [::]:12345 -> [2607:f8b0:400d:c0d::8b]:80: success
    [+] local address = [2603:3001:2d00:1800:d43c:6ba4:25c3:5a3a]:12345
    [+] target = [2607:f8b0:400d:c0d::8b]:80
    [+] local address = [::]:12345
    [-] connect(): [::]:12345 -> [2607:f8b0:400d:c0d::8b]:80: Address already in use

As we see, since we haven't specified a local address, the program will
choose the `AF_UNSPEC` so that it's agnostic as to using IPv4 or IPv6.
Our DNS lookup for `google.com` returns 14 results, the first of which
is an IPv6 address, so we use that address to connect to. After the call
to `connect()`, we can see which address was chosen for the local side
of the connection.

we then loop around and do it again for the same remote target and local
address/port, but then we can an error "Address already in use", because we
are forcing it to use the same combination as an existing connection.

The following is a demonstration of what happens if we run the program
attemptign to use `0` as the local port. This indicates to the API to
choose a port for us:

    $ bin/tcp-client-bind google.com 80 0
    [+] local address family = AF_UNSPEC
    [+] getaddrinfo(): returned 14 addresses
    [+] target = [2607:f8b0:400d:c0d::8b]:80
    [+] local address = [::]:0
    [+] connect(): [::]:0 -> [2607:f8b0:400d:c0d::8b]:80: success
    [+] local address = [2603:3001:2d00:1800:d43c:6ba4:25c3:5a3a]:51640
    [+] target = [2607:f8b0:400d:c0d::8b]:80
    [+] local address = [::]:0
    [+] connect(): [::]:0 -> [2607:f8b0:400d:c0d::8b]:80: success
    [+] local address = [2603:3001:2d00:1800:d43c:6ba4:25c3:5a3a]:51641

In this case, both connections have succeeded, where the system chose
port `51640` as the source for the first connection, and port `51641`
as the source for the next connection.

The following is an example running of this program with a specified
source IP address of `10.1.10.74`:

    $ bin/tcp-client-bind google.com 80 12345 10.1.10.74
    [+] local address family = AF_INET v4
    [+] getaddrinfo(): returned 12 addresses
    [+] target = [209.85.232.139]:80
    [+] local address = [10.1.10.74]:12345
    [+] connect(): [10.1.10.74]:12345 -> [209.85.232.139]:80: success
    [+] local address = [10.1.10.74]:12345
    [+] target = [209.85.232.139]:80
    [+] local address = [10.1.10.74]:12345
    [-] connect(): [10.1.10.74]:12345 -> [209.85.232.139]:80: Address already in use

Because this is an IPv4 address, I've forced the DNS lookup to
constrain itself to IPv4, so it returns 12 results instead of 14. It then
uses IPv4 for the rest of the steps. Like the previous example, it
errors on the second connect, because the combination of ports/addresses
is already in use.

The correct way to resolve this is, of course, to `bind()` from a different
port. But it's also worth noting that instead, you could connect to one of
the other IP addresses returned in the DNS lookup.

Conclusion
---

The `bind()` function specifies which local port to use. This is essential
for writing servers that listen on a port, to specify which one will be used.
But it's optional for clients, where normally the operating-system chooses
an appropriate port and IP address for you.

This program can also be used to demonstrate how the combination of:

    [source-address:source-port] -> [dest-address:dest-port]

must be unique, and that trying to force a duplicate connection will
result in an error.

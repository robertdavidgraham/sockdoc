#include <errno.h> /* errno */
#include <stdio.h>
#include <stdlib.h> /* malloc() */
#include <string.h> /* strerror() */
#include <time.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h> 	/* SIOCGIFCONF */
#include <netdb.h>		/* getnameinfo() */
#include <ifaddrs.h> 	/* getifaddrs() struct ifaddrs */


/**
 * List the interfaces on the local machine and IP addresses
 */
void list_interfaces(FILE *fp)
{
    struct ifaddrs *iflist;	/* head of the linked list */
	struct ifaddrs *ifa;	/* iterates through the linked list */
	int err;
	char family[16];

	/* Ask the kernel for a linked-list of network adapters */
	err = getifaddrs(&iflist);
    if (err == -1) {
		fprintf(fp, "list_interfaces: getifaddrs: %s\n", strerror(errno));
		return;
    }

	/* Enumerate the returned list */
    for (ifa = iflist; ifa != NULL; ifa = ifa->ifa_next) {
    	char addrname[256];

		/* If there is no address associated with the adapter, still print it */
		if (ifa->ifa_addr == NULL) {
			fprintf(fp, "%-16s --empty--\n", ifa->ifa_name);
			continue;
		}

		switch (ifa->ifa_addr->sa_family) {
			case AF_INET: memcpy(family, "IPv4", 5); break;
			case AF_INET6: memcpy(family, "IPv6", 5); break;
			case AF_LINK: memcpy(family, "link", 5); break;
			default: snprintf(family, sizeof(family), "%d", ifa->ifa_addr->sa_family); break;
		}

		err = getnameinfo(ifa->ifa_addr, ifa->ifa_addr->sa_family,
							addrname, sizeof(addrname),
							0, 0,
							NI_NUMERICHOST);
		if (err == -1) {
			fprintf(fp, "list_interfaces: getnameinfo: %s: %s\n", ifa->ifa_name, gai_strerror(err));
			continue;
		}

		fprintf(fp, " %-16s %-6s  %s\n", ifa->ifa_name, family, addrname);

	}

    freeifaddrs(iflist);
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
	
	/* List the network interfaces */
	printf("--- network interfaces ----\n");
	list_interfaces(stdout);
	return 0;
}


/*
    This lists interfaces and addresses on the local machine.
    Linux:
        Use getifaddrs(), AF_PACKET is for the MAC addresses.
    BSD/macOS:
        Use getifaddrs, AF_LINK is for the MAC addresses.
    AIX:
        Use SIOCGIFCONF for both IPv4 and IPv6 addresses.
    Solaris:
        Use SIOCGIFCONF for IPv4.
        Use SIOCGLIFCONF for IPv6.
    HP/UX:
        Use SIOCGIFCONF for IPv4.
        Use SIOCGLIFCONF for IPv6.
    QNX:
        Use getifaddrs().
    VxWorks:
        Use SIOCGIFCONF.
    
*/
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


#include <unistd.h>
#include <netdb.h>		/* getnameinfo() */
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <net/if.h> 	/* SIOCGIFCONF */
#include <ifaddrs.h> 	/* getifaddrs() struct ifaddrs */

#ifdef AF_PACKET /* Linux */
#include <linux/if.h>
#include <linux/if_packet.h>
#endif

void list_interfaces1(FILE *fp)
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
#ifdef AF_LINK /* BSD */
			case AF_LINK: memcpy(family, "link", 5); break;
#endif
#ifdef AF_PACKET /* Linux */
			case AF_PACKET: memcpy(family, "pkt", 4); break;
#endif
			default: snprintf(family, sizeof(family), "%d", ifa->ifa_addr->sa_family); break;
		} 

		switch (ifa->ifa_addr->sa_family) {
#ifdef AF_PACKET /* Linux */
			case AF_PACKET:
				{
					struct sockaddr_ll *s = (struct sockaddr_ll*)ifa->ifa_addr;
					snprintf(addrname, sizeof(addrname), "%02x:%02x:%02x:%02x:%02x:%02x",
						s->sll_addr[0], s->sll_addr[1], s->sll_addr[2], 
						s->sll_addr[3], s->sll_addr[4], s->sll_addr[5]
						);
				}
				break;
#endif
			default:
				err = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_storage),
									addrname, sizeof(addrname),
									0, 0,
									NI_NUMERICHOST);
				if (err < 0) {
					fprintf(fp, "list_interfaces: getnameinfo: %s: %s\n", ifa->ifa_name, gai_strerror(err));
					continue;
				}
				break;
		}
		fprintf(fp, " %-16s %-6s  %s\n", ifa->ifa_name, family, addrname);

	}

    freeifaddrs(iflist);
}
int main()
{
    struct ifreq *ifr;
    struct ifconf ifc;
    int fd;
    int i;
    int numif;
    int err;

    // find number of interfaces.
    memset(&ifc, 0, sizeof(ifc));
    ifc.ifc_ifcu.ifcu_req = NULL;
    ifc.ifc_len = 0;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        fprintf(stderr, "listif:socket(): %s\n", strerror(errno));
        return -1;
    }

    err = ioctl(fd, SIOCGIFCONF, &ifc);
    if (err < 0) {
        fprintf(stderr, "listif:ioctl(): %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    if ((ifr = malloc(ifc.ifc_len)) == NULL) {
    perror("malloc");
    exit(3);
    }
    ifc.ifc_ifcu.ifcu_req = ifr;

    if (ioctl(fd, SIOCGIFCONF, &ifc) < 0) {
    perror("ioctl2");
    exit(4);
    }
    close(fd);

    numif = ifc.ifc_len / sizeof(struct ifreq);
    for (i = 0; i < numif; i++) {
    struct ifreq *r = &ifr[i];
    struct sockaddr_in *sin = (struct sockaddr_in *)&r->ifr_addr;

    printf("%-8s : %s\n", r->ifr_name, inet_ntoa(sin->sin_addr));
    }

    free(ifr);
    exit(0);
}
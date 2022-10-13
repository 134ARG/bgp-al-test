
#include "ifaddrs.h"
#include "utils/logger/logger.h"
#include <net/if.h>
#include <stdlib.h>

int
get_addr_str(struct sockaddr* ifa_addr, char* str)
{
	return getnameinfo(ifa_addr,
	                   (ifa_addr->sa_family == AF_INET)
	                       ? sizeof(struct sockaddr_in)
	                       : sizeof(struct sockaddr_in6),
	                   str,
	                   NI_MAXHOST,
	                   NULL,
	                   0,
	                   NI_NUMERICHOST);
}

struct ifaddrs*
get_valid_ifs(struct ifaddrs* ifap, int accept_ipv6, int accept_lo)
{
	struct ifaddrs* current_ifap = ifap;
	struct ifaddrs* prev_ifap    = NULL;
	struct ifaddrs* ret_addr     = NULL;

	int ifcout = 0;
	ret_addr   = current_ifap;
	while (current_ifap) {
		++ifcout;

		char host[NI_MAXHOST];
		int  invalid = 0;

		LOG_INFO("checking no.%d interface.", ifcout);
		char* ifname = current_ifap->ifa_name;
		LOG_INFO("[%s] name of the interface: %s", ifname, ifname);

		if (get_addr_str(current_ifap->ifa_addr, host)) {
			LOG_INFO("[%s] ip address: no", ifname);
			invalid = 1;
		} else {
			LOG_INFO("[%s] ip address: %s", ifname, host);
			if (!accept_ipv6 && current_ifap->ifa_addr->sa_family == AF_INET6) {
				invalid = 1;
			}
		}

		unsigned int flags = current_ifap->ifa_flags;
		if (flags & IFF_BROADCAST) {
			LOG_INFO("[%s] interface type: broadcast", ifname);
		} else if (flags & IFF_POINTOPOINT) {
			LOG_INFO("[%s] interface type: point to point", ifname);
		} else if (flags & IFF_LOOPBACK) {
			LOG_INFO("[%s] interface type: loopback", ifname);
			if (!accept_lo) {
				invalid = 1;
			}
		} else {
			LOG_INFO("[%s] interface type: other", ifname);
			invalid = 1;
		}

		struct ifaddrs* next = current_ifap->ifa_next;
		if (invalid) {
			LOG_INFO("[%s] skipping current interface", ifname);
			if (current_ifap == ret_addr) {
				ret_addr = next;
			} else {
				prev_ifap->ifa_next = next;
			}
		} else {
			LOG_INFO("[%s] keeping current interface", ifname);
			prev_ifap = current_ifap;
		}
		current_ifap = next;
	}

	return ret_addr;
}

unsigned int
len_ifs(struct ifaddrs* ifs)
{
	unsigned int length = 0;
	while (ifs) {
		++length;
		ifs = ifs->ifa_next;
	}
	return length;
}

void
print_ifaddrs(struct ifaddrs* ifap)
{
	while (ifap) {
		char host[NI_MAXHOST];

		printf("name of the interface: %s\n", ifap->ifa_name);
		struct sockaddr_in* addr = (struct sockaddr_in*)ifap->ifa_addr;
		printf("port number: %d\n", addr->sin_port);

		if (get_addr_str(ifap->ifa_addr, host)) {
			printf("ip address: no\n");
		} else {
			printf("ip address: %s\n", host);
		}

		if (ifap->ifa_flags & IFF_BROADCAST) {
			printf("interface type: broadcast\n");
			struct sockaddr* baddr = ifap->ifa_broadaddr;
			if (get_addr_str(baddr, host)) {
				printf("broadcast address: no\n");
			} else {
				printf("broadcast address: %s\n", host);
			}
		} else if (ifap->ifa_flags & IFF_POINTOPOINT) {
			printf("interface type: point to point\n");
		} else if (ifap->ifa_flags & IFF_LOOPBACK) {
			printf("interface type: loopback\n");
		} else {
			printf("interface type: other\n");
		}
		printf("\n");

		ifap = ifap->ifa_next;
	}
}

struct ifaddrs*
find_matched_if(struct ifaddrs* all_ifs, struct sockaddr_in* addr)
{
	char addr_str[NI_MAXHOST];
	if (get_addr_str((struct sockaddr*)addr, addr_str)) {
		LOG_DEBUG("finding for address: %s", addr_str);
	}
	LOG_DEBUG("finding interface");

	struct ifaddrs* current = all_ifs;

	in_addr_t addr_in = addr->sin_addr.s_addr;

	while (current) {
		in_addr_t mask =
		    ((struct sockaddr_in*)current->ifa_netmask)->sin_addr.s_addr;
		in_addr_t addr_if =
		    ((struct sockaddr_in*)current->ifa_addr)->sin_addr.s_addr;

		if ((addr_in & mask) == (addr_if & mask)) {
			LOG_DEBUG("interface found: %s", current->ifa_name);
			return current;
		}
		current = current->ifa_next;
	}
	LOG_DEBUG("interface not found. skip.");
	return NULL;
}

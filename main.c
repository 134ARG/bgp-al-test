#include "logger/logger.h"
#include "vector/vector.h"
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <memory.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <printf.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BROADCAST_PORT 5151
// #define RECV_PORT      5152

#define SEND_TRY 3

#define MAX_MESSAGE_SIZE 4096

struct record {
	enum {
		RWITHDRAW = 0,
		RNEXT_HOP,
		RHOST_PATH,
	} type;
	in_addr_t addr;
	in_addr_t gateway;
};

struct message {
	unsigned int size;
	enum {
		MADD = 0,
		MWITHDRAW,
	} type;
	unsigned int  record_len;
	struct record record_list[];
};

struct routing_entry {
	int             weight;
	in_addr_t       mask;
	in_addr_t       base;
	in_addr_t       gateway;
	struct ifaddrs* if_addr;
};

INITIALIZE_VECTOR(routing_vector, struct routing_entry)

routing_vector routing_table;

int
route_aggregation(in_addr_t* mask, in_addr_t* base, in_addr_t* new)
{
	// TODO(134ARG): to be implemented
	if ((*new&* mask) == (*base & *mask)) {
		return 0;
	}
	return 1;
}

int
add_new_route(in_addr_t       dest,
              in_addr_t       gateway,
              unsigned int    weight,
              struct ifaddrs* if_addr)
{
	struct routing_entry* r;

	in_addr_t mask = 0;
	in_addr_t base = 0;
	in_addr_t new  = 0;

	for (size_t i = 0; i < routing_table.length; ++i) {
		CHECK_OK(routing_vector_get_pointer(&routing_table, i, &r));
		mask    = r->mask;
		base    = r->base;
		new     = dest;
		int ret = route_aggregation(&mask, &base, &new);
		if (!ret && r->gateway == gateway && r->if_addr == if_addr) {
			return 0;
		}
	}

	r->base    = dest;
	r->mask    = (in_addr_t)-1;
	r->gateway = gateway;
	r->weight  = weight;
	r->if_addr = if_addr;

	return 0;
}

// use inet_pton() to set ip address, example:
// 	struct sockaddr_in* addr = (struct sockaddr_in*)&ifr.ifr_addr;
// 	inet_pton(AF_INET, "10.12.0.1", &addr->sin_addr);

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
		if (current_ifap->ifa_flags & IFF_BROADCAST) {
			LOG_INFO("[%s] interface type: broadcast", ifname);
		} else if (current_ifap->ifa_flags & IFF_POINTOPOINT) {
			LOG_INFO("[%s] interface type: point to point", ifname);
		} else if (current_ifap->ifa_flags & IFF_LOOPBACK) {
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

int
broadcast_message(struct ifaddrs* ifap, char* msg, int len)
{
	int socketfd;

	if ((socketfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		LOG_ERROR("failed to create socket. errno: %d", errno);
		return -1;
	}

	while (ifap) {
		struct sockaddr_in dest_addr;
		memset(&dest_addr, 0, sizeof(dest_addr));
		struct sockaddr_in* if_addr = NULL;

		if (ifap->ifa_flags & IFF_BROADCAST) {
			if_addr              = (struct sockaddr_in*)ifap->ifa_broadaddr;
			int broadcast_enable = 1;
			int ret              = setsockopt(socketfd,
                                 SOL_SOCKET,
                                 SO_BROADCAST,
                                 &broadcast_enable,
                                 sizeof(broadcast_enable));
			if (ret < 0) {
				LOG_WARN("[%s] can not set broadcast enabled. errno: %d. SKIP",
				         ifap->ifa_name,
				         errno);
				goto NEXT_ITER;
			}
		} else if (ifap->ifa_flags & IFF_POINTOPOINT ||
		           ifap->ifa_flags & IFF_LOOPBACK) {
			if_addr = (struct sockaddr_in*)ifap->ifa_dstaddr;
		} else {
			LOG_INFO("[%s] skipping current interface for unsupported flag",
			         ifap->ifa_name);
			goto NEXT_ITER;
		}

		dest_addr.sin_family      = AF_INET;
		dest_addr.sin_port        = htons(BROADCAST_PORT);
		dest_addr.sin_addr.s_addr = if_addr->sin_addr.s_addr;

		int retry = 0;
		while (retry < SEND_TRY) {
			int ret = sendto(socketfd,
			                 msg,
			                 len,
			                 MSG_CONFIRM,
			                 (const struct sockaddr*)&dest_addr,
			                 sizeof(dest_addr));
			if (ret < 0) {
				++retry;
				LOG_WARN("[%s] message send failed. times %d, errno %d",
				         ifap->ifa_name,
				         retry,
				         errno);

			} else {
				break;
			}
		}
		if (retry >= SEND_TRY) {
			LOG_WARN("[%s] message send failed. all retry failed. SKIP",
			         ifap->ifa_name);
		} else {
			LOG_INFO("[%s] message sent", ifap->ifa_name);
		}

	NEXT_ITER:
		ifap = ifap->ifa_next;
	}

	close(socketfd);
	return 0;
}

int
recv_message(in_addr_t in_addr, char* buffer, int len)
{
	int socketfd;

	struct sockaddr_in recv_addr;
	struct sockaddr_in sender_addr;
	memset(&recv_addr, 0, sizeof(recv_addr));
	memset(&sender_addr, 0, sizeof(sender_addr));

	if ((socketfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		LOG_ERROR("failed to create socket. errno: %d", errno);
		return -1;
	}

	recv_addr.sin_family      = AF_INET;
	recv_addr.sin_port        = htons(BROADCAST_PORT);
	recv_addr.sin_addr.s_addr = in_addr;

	if (bind(socketfd, (const struct sockaddr*)&recv_addr, sizeof(recv_addr)) <
	    0) {
		LOG_ERROR("failed to bind the socket. errno: %d", errno);
		return -1;
	}

	unsigned int addrlen = sizeof(sender_addr);

	int n = recvfrom(socketfd,
	                 buffer,
	                 len,
	                 MSG_WAITFORONE,
	                 (struct sockaddr*)&sender_addr,
	                 &addrlen);
	if (n < 0) {
		LOG_ERROR("message receive failed. errno: %d", errno);
		return -1;
	}
	char addr_str[NI_MAXHOST];
	if (get_addr_str((struct sockaddr*)&sender_addr, addr_str)) {
		strcpy(addr_str, "no addr");
	}
	LOG_INFO("message received. sender address: %s", addr_str);
	return n;
}

int
recv_message_from_if(struct ifaddrs* if_addr, char* buffer, int len)
{
	return recv_message(
	    ((struct sockaddr_in*)if_addr->ifa_addr)->sin_addr.s_addr,
	    buffer,
	    len);
}

int
main(void)
{
	set_log_level(LDEBUG);
	routing_table = make_routing_vector();

	char test_buffer[20];
	memset(test_buffer, 0, 20);

	struct ifaddrs* ifap = NULL;

	// int ret = getifaddrs(&ifap);

	int ret = getifaddrs(&ifap);
	if (ret) {
		LOG_ERROR("failed to get ifs. errno: %d", errno);
		return 0;
	}
	struct ifaddrs* filtered_ifap = get_valid_ifs(ifap, 0, 1);
	print_ifaddrs(filtered_ifap);
	broadcast_message(filtered_ifap, "something", sizeof("something"));
	if (recv_message(INADDR_ANY, test_buffer, MAX_MESSAGE_SIZE) > 0) {
		LOG_INFO("received message: %s", test_buffer);
	} else {
		LOG_WARN("nothing received.");
	}
	freeifaddrs(ifap);
	clean_routing_vector(&routing_table);

	return 0;
}

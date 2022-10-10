#include "logger/logger.h"
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <memory.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <printf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

// union seg4_addr {
// 	struct {
// 		u_int8_t net;
// 		u_int8_t host;
// 		u_int8_t lh;
// 		u_int8_t imp;
// 	} seg;
// 	u_int32_t s_addr;
// };

#define BROADCAST_PORT 5151
// #define RECV_PORT      5152

#define SEND_TRY 3

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
		struct sockaddr_in server_addr;
		memset(&server_addr, 0, sizeof(server_addr));
		struct sockaddr_in* dest_addr = NULL;

		if (ifap->ifa_flags & IFF_BROADCAST) {
			dest_addr            = (struct sockaddr_in*)ifap->ifa_broadaddr;
			int broadcast_enable = 1;
			int ret              = setsockopt(socketfd,
                                 SOL_SOCKET,
                                 SO_BROADCAST,
                                 &broadcast_enable,
                                 sizeof(broadcast_enable));
			if (ret < 0) {
				LOG_WARN("[%s] can not set broadcast enabled. SKIP",
				         ifap->ifa_name);
				goto NEXT_ITER;
			}
		} else if (ifap->ifa_flags & IFF_POINTOPOINT ||
		           ifap->ifa_flags & IFF_LOOPBACK) {
			dest_addr = (struct sockaddr_in*)ifap->ifa_dstaddr;
		} else {
			LOG_INFO("[%s] skipping current interface", ifap->ifa_name);
			goto NEXT_ITER;
		}

		server_addr.sin_family      = AF_INET;
		server_addr.sin_port        = htons(BROADCAST_PORT);
		server_addr.sin_addr.s_addr = dest_addr->sin_addr.s_addr;

		int retry = 0;
		while (retry < SEND_TRY) {
			int ret = sendto(socketfd,
			                 msg,
			                 len,
			                 MSG_CONFIRM,
			                 (const struct sockaddr*)&server_addr,
			                 sizeof(server_addr));
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

	return 0;
}

int
recv_message(char* buffer, int len)
{
	int socketfd;

	struct sockaddr_in server_addr;
	struct sockaddr_in sender_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	memset(&sender_addr, 0, sizeof(sender_addr));

	if ((socketfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		LOG_ERROR("failed to create socket. errno: %d", errno);
		return -1;
	}

	server_addr.sin_family      = AF_INET;
	server_addr.sin_port        = htons(BROADCAST_PORT);
	server_addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(socketfd,
	         (const struct sockaddr*)&server_addr,
	         sizeof(server_addr)) < 0) {
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
main(void)
{
	set_log_level(LDEBUG);

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
	if (recv_message(test_buffer, 20) > 0) {
		LOG_INFO("received message: %s", test_buffer);
	} else {
		LOG_WARN("nothing received.");
	}
	freeifaddrs(ifap);

	return 0;
}

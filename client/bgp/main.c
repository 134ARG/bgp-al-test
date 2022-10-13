#include "protocol/bgp/message.h"
#include "routing/bgp/routing.h"
#include "utils/logger/logger.h"
#include "utils/mem/mem_utils.h"
#include <arpa/inet.h>
#include <bits/pthreadtypes.h>
#include <errno.h>
#include <ifaddrs.h>
#include <memory.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <printf.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BROADCAST_PORT 5151

#define SEND_TRY 3

#define MAX_MESSAGE_SIZE 4096

u_int64_t host_id = 0;

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

void
close_socket(int* socketfd)
{
	close(*socketfd);
}

int
broadcast_message_from_if(struct ifaddrs* ifap, char* msg, int len)
{
	CLEANUP(close_socket) int socketfd;

	if ((socketfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		LOG_ERROR("failed to create socket. errno: %d", errno);
		return -1;
	}

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
			return -1;
		}
	} else if (ifap->ifa_flags & IFF_POINTOPOINT ||
	           ifap->ifa_flags & IFF_LOOPBACK) {
		if_addr = (struct sockaddr_in*)ifap->ifa_dstaddr;
	} else {
		LOG_INFO("[%s] skipping current interface for unsupported flag",
		         ifap->ifa_name);
		return -1;
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

	return 0;
}

int
recv_message(in_addr_t           in_addr,
             char*               buffer,
             int                 len,
             struct sockaddr_in* sender_return)
{
	CLEANUP(close_socket) int socketfd;

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
	if (sender_return) {
		*sender_return = sender_addr;
	}

	return n;
}

int
recv_message_from_if(struct ifaddrs* if_addr, char* buffer, int len)
{
	return recv_message(
	    ((struct sockaddr_in*)if_addr->ifa_addr)->sin_addr.s_addr,
	    buffer,
	    len,
	    NULL);
}

int
broadcast_update(struct ifaddrs* all_ifs, struct update_message* m_ptr)
{
	struct ifaddrs* current      = all_ifs;
	int             with_failure = 0;

	LOG_INFO("start broadcast.");
	while (current) {
		int n = broadcast_message_from_if(current, (char*)m_ptr, m_ptr->size);
		if (n < 0) {
			LOG_WARN("broadcast update message failed at %s",
			         current->ifa_name);
			with_failure = 1;
		}
		current = current->ifa_next;
	}
	if (with_failure) {
		LOG_WARN("sending update failed on some interfaces.");
	} else {
		LOG_INFO("sending update success.");
	}
	return 0;
}

int
self_update(struct ifaddrs* all_ifs)
{
	int with_failure = 0;

	struct ifaddrs*                     current = all_ifs;
	CLEANUP_FREE struct update_message* m_ptr   = make_message();

	m_ptr->type   = MADD;
	m_ptr->weight = 1;
	m_ptr         = add_aspath(m_ptr, host_id);

	while (current) {
		m_ptr->addr = ((struct sockaddr_in*)current->ifa_addr)->sin_addr.s_addr;
		m_ptr->gateway = 0;  // TODO(134ARG)
		int n = broadcast_message_from_if(current, (char*)m_ptr, m_ptr->size);
		if (n < 0) {
			LOG_WARN("broadcast update message failed at %s",
			         current->ifa_name);
			with_failure = 1;
		}
		current = current->ifa_next;
	}

	if (with_failure) {
		LOG_WARN("sending update failed on some interfaces.");
	} else {
		LOG_INFO("sending update success.");
	}
	return 0;
}

struct ifaddrs*
find_recv_if(struct ifaddrs* all_ifs, struct sockaddr_in* addr)
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

struct thread_args {
	struct ifaddrs*        all_ifs;
	struct routing_table*  table;
	struct update_message* m_ptr_own;
	struct sockaddr_in     sender_addr;
};

struct thread_args*
make_thread_args()
{
	return calloc(1, sizeof(struct thread_args));
}

void
milisecond_sleep(unsigned long msec)
{
	struct timespec ts;
	ts.tv_sec  = msec / 1000;
	ts.tv_nsec = (msec % 1000) * 1000000;
	nanosleep(&ts, &ts);
}

void*
decision(void* arg_own)
{
	CLEANUP_FREE struct thread_args* args = (struct thread_args*)arg_own;

	CLEANUP_FREE struct update_message* m_ptr = args->m_ptr_own;

	struct ifaddrs* recv_if = find_recv_if(args->all_ifs, &(args->sender_addr));
	if (!recv_if) {
		LOG_WARN("message from unkonwn source. dispose.");
		return (void*)-1;
	} else {
		LOG_INFO("receiver found. start decision process.");
	}

	if (!check_if_valid_ASPATH(m_ptr, host_id)) {
		LOG_INFO(
		    "[%s] circle detected. invalid ASPATH. skip current update message.",
		    recv_if->ifa_name);
		return (void*)0;
	}

	struct route_entry new_route = make_routing_from_update(m_ptr, recv_if);

	if (m_ptr->type == MWITHDRAW) {
		LOG_INFO("WITHDRAW update.");
		if (withdraw_route(args->table, &new_route) == SWITHDREW) {
			LOG_INFO("WITHDRAW finished. start broadcast to peers");
			broadcast_update(args->all_ifs, m_ptr);
		} else {
			LOG_INFO("No new update.");
		}

	} else if (m_ptr->type == MADD) {
		LOG_INFO("ADD update received.");
		if (add_new_route(args->table, &new_route) == SNEW) {
			LOG_INFO("ADD finished. start broadcast to peers");
			m_ptr = add_aspath(m_ptr, host_id);
			++(m_ptr->weight);
			broadcast_update(args->all_ifs, m_ptr);

			milisecond_sleep(20);

			LOG_INFO("start new self broadcasting");
			self_update(args->all_ifs);

			log_routing_table(args->table);
		} else {
			LOG_INFO("No new update.");
		}
	}

	return (void*)0;
}

void*
receive_main_loop(void* arg_own)
{
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

	CLEANUP_FREE struct thread_args* args = (struct thread_args*)arg_own;

	while (1) {
		char               buffer[MAX_MESSAGE_SIZE];
		struct sockaddr_in sender_addr;

		int n =
		    recv_message(INADDR_ANY, buffer, MAX_MESSAGE_SIZE, &sender_addr);
		if (n < 0) {
			LOG_WARN("failed to receive message. skip.");
		} else {
			LOG_INFO("message received.");

			struct thread_args* decision_args_own = make_thread_args();

			decision_args_own->all_ifs = args->all_ifs;
			decision_args_own->m_ptr_own =
			    make_message_from_buffer(buffer, MAX_MESSAGE_SIZE);
			decision_args_own->sender_addr = sender_addr;
			decision_args_own->table       = args->table;

			pthread_t tid;

			int ret = pthread_create(&tid,
			                         NULL,
			                         decision,
			                         (void*)MOVE_OUT(decision_args_own));
			if (ret) {
				LOG_ERROR("failed to create thread. abort.");
			} else {
				pthread_detach(tid);
				LOG_INFO("thread for receive message dispatched. tid: %lu",
				         tid);
			}
		}
		pthread_testcancel();
	}
}

int
dispatch(struct ifaddrs* all_ifs, struct routing_table* table, pthread_t* tid)
{
	struct thread_args* args = make_thread_args();

	args->all_ifs = all_ifs;
	args->table   = table;

	int ret =
	    pthread_create(tid, NULL, receive_main_loop, (void*)MOVE_OUT(args));
	if (ret) {
		LOG_ERROR("failed to create thread. abort.");
		return ret;
	}
	pthread_detach((*tid));

	LOG_INFO("thread for all interfaces created and dispatched.");

	return 0;
}

// TODO(134ARG): refactor
int
execute_command(char                  command,
                struct ifaddrs*       all_ifs,
                struct routing_table* table)
{
	const char self_broadcast_cmd    = 'b';
	const char log_routing_table_cmd = 'r';
	const char quit_cmd              = 'q';
	const char enter                 = '\n';

	if (command == self_broadcast_cmd) {
		self_update(all_ifs);
	} else if (command == log_routing_table_cmd) {
		log_routing_table(table);
	} else if (command == quit_cmd) {
		return -1;
	} else if (command == enter) {
		;
	} else {
		LOG_ERROR("unknown command.");
	}
	return 0;
}

int
main(void)
{
	set_log_level(LDEBUG);

	CLEANUP(free_routing_table) struct routing_table table;
	LIST_INIT(&table);

	char test_buffer[20];
	memset(test_buffer, 0, 20);

	struct ifaddrs* ifap = NULL;

	srand(time(NULL));
	host_id = rand();
	LOG_INFO("report host id: %lu", host_id);

	int ret = getifaddrs(&ifap);
	if (ret) {
		LOG_ERROR("failed to get ifs. errno: %d", errno);
		return 0;
	}

	struct ifaddrs* selected_ifs = get_valid_ifs(ifap, 0, 0);

	print_ifaddrs(selected_ifs);

	self_update(selected_ifs);

	pthread_t tid;
	if (dispatch(selected_ifs, &table, &tid)) {
		LOG_ERROR("thread creation failed. exit.");
		goto CLEAN_UP;
	}

	char        cmd[20];
	const char* prompt = "> ";

	while (1) {
		printf("cli host-%lu %s", host_id, prompt);
		fgets(cmd, 20, stdin);
		int ret = execute_command(cmd[0], selected_ifs, &table);
		if (ret) {
			pthread_cancel(tid);
			break;
		}
	}

CLEAN_UP:
	freeifaddrs(ifap);

	return 0;
}

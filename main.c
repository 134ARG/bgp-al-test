#include "./mem/mem_utils.h"
#include "logger/logger.h"
#include "vector/vector.h"
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
// #define RECV_PORT      5152

#define SEND_TRY 3

#define MAX_MESSAGE_SIZE 4096

struct ifaddrs* filtered_ifap = NULL;

u_int64_t host_id = 0;

struct update_message {
	u_int32_t size;
	u_int32_t path_len;
	enum {
		MADD = 0,
		MWITHDRAW,
	} type;
	in_addr_t addr;
	in_addr_t gateway;
	u_int32_t weight;
	u_int64_t ASPATH[];
};

struct update_message*
make_message()
{
	struct update_message* p = calloc(1, sizeof(struct update_message));
	p->size                  = sizeof(*p);
	return p;
}

void
free_message(struct update_message* ptr)
{
	free(ptr);
}

struct routing_entry {
	u_int32_t       weight;
	in_addr_t       mask;
	in_addr_t       base;
	in_addr_t       gateway;
	struct ifaddrs* if_addr;
	LIST_ENTRY(routing_entry) entries;
};

int
routing_entry_eq(struct routing_entry* a, struct routing_entry* b)
{
	return (a->base == b->base) && (a->mask == b->mask) &&
	       (a->gateway == b->gateway) && (a->if_addr == b->if_addr) &&
	       (a->weight == b->weight);
}

int
copy_routing_entry(struct routing_entry* src, struct routing_entry* dest)
{
	if (!src || !dest) {
		LOG_ERROR("null pointer when copying routing entry");
		return -1;
	}
	dest->weight  = src->weight;
	dest->mask    = src->mask;
	dest->base    = src->base;
	dest->gateway = src->gateway;
	dest->if_addr = src->if_addr;
	return 0;
}

#ifndef LIST_FOREACH_SAFE
#define LIST_FOREACH_SAFE(var, head, field, tvar)                              \
	for ((var) = LIST_FIRST((head));                                           \
	     (var) && ((tvar) = LIST_NEXT((var), field), 1);                       \
	     (var) = (tvar))
#endif

LIST_HEAD(, routing_entry) routing_table;

void
log_routing_table()
{
	struct routing_entry* current;

	union seg4_addr {
		struct {
			u_int8_t seg1;
			u_int8_t seg2;
			u_int8_t seg3;
			u_int8_t seg4;
		} addr;
		in_addr_t raw;
	};

	LOG_INFO("start logging routing table");
	LIST_FOREACH (current, &routing_table, entries) {
		union seg4_addr base = {.raw = current->base};
		LOG_INFO("\tbase:%d:%d:%d:%d",
		         base.addr.seg1,
		         base.addr.seg2,
		         base.addr.seg3,
		         base.addr.seg4);
		union seg4_addr gateway = {.raw = current->gateway};
		LOG_INFO("\tgateway:%d:%d:%d:%d",
		         gateway.addr.seg1,
		         gateway.addr.seg2,
		         gateway.addr.seg3,
		         gateway.addr.seg4);
		LOG_INFO("\tweight: %d", current->weight);
		LOG_INFO("\tif_name: %s", current->if_addr->ifa_name);
	}
	LOG_INFO("logging routing table finished");
}

void
free_routing_table()
{
	struct routing_entry* current;
	struct routing_entry* temp;
	LIST_FOREACH_SAFE (current, &routing_table, entries, temp) {
		free(current);
	}
	LIST_INIT(&routing_table);
}

// TODO(134ARG): optimize
struct update_message*
add_aspath(struct update_message* m_ptr, u_int64_t new_host_id)
{
	unsigned int original_len = m_ptr->path_len;
	unsigned int original_size =
	    sizeof(struct update_message) + original_len * sizeof(u_int64_t);
	unsigned int new_size =
	    sizeof(struct update_message) + (original_len + 1) * sizeof(u_int64_t);
	struct update_message* new_p = malloc(new_size);
	memcpy(new_p, m_ptr, original_size);
	memcpy(&(new_p->ASPATH)[original_len], &new_host_id, sizeof(u_int64_t));

	++(new_p->path_len);
	new_p->size = new_size;

	LOG_INFO("ASPATH added.");

	return new_p;
}

int
route_aggregate(struct routing_entry* new, struct routing_entry* old)
{
	// TODO(134ARG): to be implemented
	return 1;
}

int
route_disaggregate(struct routing_entry* new, struct routing_entry* old)
{
	// TODO(134ARG): to be implemented
	return 1;
}

enum add_status {
	SNEW = 0,
	SEXISTED,
};

enum add_status
add_new_route(struct routing_entry* new)
{
	struct routing_entry* current;

	LIST_FOREACH (current, &routing_table, entries) {
		int ret = route_aggregate(new, current);
		if (!ret) {
			return SEXISTED;
		}

		if (routing_entry_eq(new, current)) {
			return SEXISTED;
		}

		if ((new->base == current->base) &&
		    (!strcmp(new->if_addr->ifa_name, current->if_addr->ifa_name)) &&
		    (new->weight < current->weight)) {
			copy_routing_entry(new, current);
			return SEXISTED;
		}
	}

	struct routing_entry* copy = malloc(sizeof(struct routing_entry));
	memcpy(copy, new, sizeof(struct routing_entry));

	LIST_INSERT_HEAD(&routing_table, copy, entries);

	return SNEW;
}

enum withdraw_status {
	SWITHDREW = 0,
	SNO,
};

int
withdraw_route(struct routing_entry* withdraw)
{
	struct routing_entry* current;
	struct routing_entry* temp;

	LIST_FOREACH_SAFE (current, &routing_table, entries, temp) {
		int ret = route_disaggregate(withdraw, current);
		if (!ret) {
			return SWITHDREW;
		}

		if (withdraw->base == current->base) {
			LIST_REMOVE(current, entries);
			free(current);
			return SWITHDREW;
		}
	}

	return SNO;
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

	// while (ifap) {
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
	// }
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

	m_ptr->type                = MADD;
	m_ptr->weight              = 1;
	struct update_message* new = add_aspath(m_ptr, host_id);
	free(m_ptr);
	m_ptr = new;

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

struct routing_entry
make_routing_from_update(struct update_message* m_ptr, struct ifaddrs* if_addr)
{
	return (struct routing_entry){
	    .weight  = m_ptr->weight,
	    .base    = m_ptr->addr,
	    .mask    = (in_addr_t)-1,
	    .gateway = m_ptr->gateway,
	    .if_addr = if_addr,
	};
}

int
check_if_valid_ASPATH(struct update_message* m_ptr)
{
	for (size_t i = 0; i < m_ptr->path_len; i++) {
		if (m_ptr->ASPATH[i] == host_id) {
			LOG_INFO("host id found in ASPATH. self: %lu, found: %lu",
			         host_id,
			         m_ptr->ASPATH[i]);
			return 0;
		}
	}
	return 1;
}

int
decision(struct ifaddrs* all_ifs,
         struct ifaddrs* recv_if,
         char*           buffer,
         int             len)
{
	struct update_message* m_ptr = (struct update_message*)buffer;
	if (m_ptr->size > len) {
		LOG_ERROR("incompelete message. parsing abort.");
		return -1;
	}

	if (!check_if_valid_ASPATH(m_ptr)) {
		LOG_INFO(
		    "[%s] circle detected. invalid ASPATH. skip current update message.",
		    recv_if->ifa_name);
		return 0;
	}

	struct routing_entry new_route = make_routing_from_update(m_ptr, recv_if);

	if (m_ptr->type == MWITHDRAW) {
		LOG_INFO("WITHDRAW update.");
		if (withdraw_route(&new_route) == SWITHDREW) {
			LOG_INFO("WITHDRAW finished. start broadcast to peers");
			broadcast_update(all_ifs, m_ptr);
		} else {
			LOG_INFO("No new update.");
		}

	} else if (m_ptr->type == MADD) {
		LOG_INFO("ADD update received.");
		if (add_new_route(&new_route) == SNEW) {
			LOG_INFO("ADD finished. start broadcast to peers");
			m_ptr = add_aspath(m_ptr, host_id);
			++(m_ptr->weight);
			broadcast_update(all_ifs, m_ptr);
			LOG_INFO("start new self broadcasting");
			self_update(all_ifs);
			free(m_ptr);
		} else {
			LOG_INFO("No new update.");
		}
	}

	return 0;
}

struct thread_arg {
	struct ifaddrs* recv_if;
	struct ifaddrs* all_ifs;
};

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

void*
receive_main_loop(void* arg)
{
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	struct ifaddrs* all_ifs = (struct ifaddrs*)arg;
	while (1) {
		char               buffer[MAX_MESSAGE_SIZE];
		struct sockaddr_in sender_addr;

		int n =
		    recv_message(INADDR_ANY, buffer, MAX_MESSAGE_SIZE, &sender_addr);
		if (n < 0) {
			LOG_WARN("failed to receive message. skip.");
		} else {
			LOG_INFO("message received.");
			struct ifaddrs* recv_if = find_recv_if(all_ifs, &sender_addr);
			if (!recv_if) {
				LOG_WARN("message from unkonwn source. dispose.");
			} else {
				LOG_INFO("receiver found. start decision process.");
				decision(all_ifs, recv_if, buffer, MAX_MESSAGE_SIZE);
				log_routing_table();
			}
		}
		pthread_testcancel();
	}
}

int
dispatch(struct ifaddrs* all_ifs, pthread_t* tid)
{
	struct ifaddrs* current = all_ifs;

	int ret = pthread_create(tid, NULL, receive_main_loop, (void*)all_ifs);
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
execute_command(char command, struct ifaddrs* all_ifs)
{
	const char self_broadcast_cmd    = 'b';
	const char log_routing_table_cmd = 'r';
	const char quit_cmd              = 'q';
	const char enter                 = '\n';

	if (command == self_broadcast_cmd) {
		self_update(all_ifs);
	} else if (command == log_routing_table_cmd) {
		log_routing_table();
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
	LIST_INIT(&routing_table);

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
	filtered_ifap = get_valid_ifs(ifap, 0, 0);

	print_ifaddrs(filtered_ifap);

	unsigned int if_length = len_ifs(filtered_ifap);

	self_update(filtered_ifap);

	pthread_t tid;
	if (dispatch(filtered_ifap, &tid)) {
		LOG_ERROR("thread creation failed. exit.");
		return 0;
	}

	char        cmd[20];
	const char* prompt = "> ";

	while (1) {
		printf("cli host-%lu %s", host_id, prompt);
		fgets(cmd, 20, stdin);
		int ret = execute_command(cmd[0], filtered_ifap);
		if (ret) {
			pthread_cancel(tid);
			break;
		}
	}

	freeifaddrs(ifap);
	free_routing_table();

	return 0;
}

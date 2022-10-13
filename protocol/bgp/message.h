#ifndef BROBABLBGP_BGP_MESSAGE_H
#define BROBABLBGP_BGP_MESSAGE_H

#include <netinet/in.h>
#include <sys/types.h>

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

struct update_message* make_message();

void free_message(struct update_message* ptr);

struct update_message* add_aspath(struct update_message* m_ptr,
                                  u_int64_t              new_host_id);

#endif  // BROBABLBGP_BGP_MESSAGE_H

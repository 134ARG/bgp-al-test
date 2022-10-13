#include "routing.h"
#include "utils/logger/logger.h"
#include <ifaddrs.h>
#include <stdlib.h>
#include <string.h>

int
equal_route_entry(struct route_entry* a, struct route_entry* b)
{
	return (a->base == b->base) && (a->mask == b->mask) &&
	       (a->gateway == b->gateway) && (a->if_addr == b->if_addr) &&
	       (a->weight == b->weight);
}

int
copy_route_entry(struct route_entry* src, struct route_entry* dest)
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

void
log_routing_table(struct routing_table* table)
{
	struct route_entry* current;

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
	LIST_FOREACH (current, table, entries) {
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
free_routing_table(struct routing_table* table)
{
	struct route_entry* current;
	struct route_entry* temp;
	LIST_FOREACH_SAFE (current, table, entries, temp) {
		free(current);
	}
	LIST_INIT(table);
}

int
route_aggregate(struct route_entry* new, struct route_entry* old)
{
	// TODO(134ARG): to be implemented
	return 1;
}

int
route_disaggregate(struct route_entry* new, struct route_entry* old)
{
	// TODO(134ARG): to be implemented
	return 1;
}

enum add_status
add_new_route(struct routing_table* table, struct route_entry* new)
{
	struct route_entry* current;

	LIST_FOREACH (current, table, entries) {
		int ret = route_aggregate(new, current);
		if (!ret) {
			return SEXISTED;
		}

		if (equal_route_entry(new, current)) {
			return SEXISTED;
		}

		if ((new->base == current->base) &&
		    (!strcmp(new->if_addr->ifa_name, current->if_addr->ifa_name)) &&
		    (new->weight < current->weight)) {
			copy_route_entry(new, current);
			return SEXISTED;
		}
	}

	struct route_entry* copy = malloc(sizeof(struct route_entry));
	memcpy(copy, new, sizeof(struct route_entry));

	LIST_INSERT_HEAD(table, copy, entries);

	return SNEW;
}

enum withdraw_status
withdraw_route(struct routing_table* table, struct route_entry* withdraw)
{
	struct route_entry* current;
	struct route_entry* temp;

	LIST_FOREACH_SAFE (current, table, entries, temp) {
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

struct route_entry
make_routing_from_update(struct update_message* m_ptr, struct ifaddrs* if_addr)
{
	return (struct route_entry){
	    .weight  = m_ptr->weight,
	    .base    = m_ptr->addr,
	    .mask    = (in_addr_t)-1,
	    .gateway = m_ptr->gateway,
	    .if_addr = if_addr,
	};
}

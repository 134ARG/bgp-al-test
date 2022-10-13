
#ifndef BROBABLBGP_BGP_ROUTING_H
#define BROBABLBGP_BGP_ROUTING_H

#include <netinet/in.h>
#include <sys/queue.h>
#include <sys/types.h>

struct route_entry {
	u_int32_t       weight;
	in_addr_t       mask;
	in_addr_t       base;
	in_addr_t       gateway;
	struct ifaddrs* if_addr;
	LIST_ENTRY(route_entry) entries;
};

LIST_HEAD(routing_table, route_entry);

#ifndef LIST_FOREACH_SAFE
#define LIST_FOREACH_SAFE(var, head, field, tvar)                              \
	for ((var) = LIST_FIRST((head));                                           \
	     (var) && ((tvar) = LIST_NEXT((var), field), 1);                       \
	     (var) = (tvar))
#endif

int equal_route_entry(struct route_entry* a, struct route_entry* b);
int copy_route_entry(struct route_entry* src, struct route_entry* dest);

void log_routing_table(struct routing_table* table);
void free_routing_table(struct routing_table* table);

int route_aggregate(struct route_entry* new, struct route_entry* existing);
int route_disaggregate(struct route_entry* new, struct route_entry* existing);

enum add_status {
	SNEW = 0,
	SEXISTED,
};

enum add_status add_new_route(struct routing_table* table,
                              struct route_entry* new);

enum withdraw_status {
	SWITHDREW = 0,
	SNO,
};

enum withdraw_status withdraw_route(struct routing_table* table,
                                    struct route_entry*   withdraw);

#endif  // BROBABLBGP_BGP_ROUTING_H

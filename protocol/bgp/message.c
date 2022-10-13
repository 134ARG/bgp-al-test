#include "message.h"
#include "utils/logger/logger.h"
#include <stdlib.h>
#include <string.h>

struct update_message*
make_message()
{
	struct update_message* p = calloc(1, sizeof(struct update_message));
	p->size                  = sizeof(*p);
	return p;
}

struct update_message*
make_message_from_buffer(char* buffer, int len)
{
	struct update_message* msg = (struct update_message*)buffer;
	if (len < msg->size) {
		LOG_ERROR("incompelete message. parsing abort.");
		return NULL;
	}
	struct update_message* ret = malloc(msg->size);
	memcpy(ret, msg, msg->size);
	return ret;
}

void
free_message(struct update_message* ptr)
{
	free(ptr);
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

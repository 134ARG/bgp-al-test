//
// Created by zry on 2021/7/8.
//

#ifndef ZLISP_MEM_UTILS_H
#define ZLISP_MEM_UTILS_H

#include <stdlib.h>

static inline void
free_pointer(void* ptr)
{
	void** p = (void**)ptr;
	free(*p);
	*p = NULL;
}

#define CLEANUP_FREE          __attribute__((__cleanup__(free_pointer)))
#define CLEANUP(CLEANUP_FUNC) __attribute__((__cleanup__(CLEANUP_FUNC)))

#define MOVE_OUT(INTERNAL_PTR)                                                 \
	__extension__({                                                            \
		__typeof__(INTERNAL_PTR) EXTERNAL_PTR = (INTERNAL_PTR);                \
		(INTERNAL_PTR)                        = NULL;                          \
		EXTERNAL_PTR;                                                          \
	})

#endif  // ZLISP_MEM_UTILS_H

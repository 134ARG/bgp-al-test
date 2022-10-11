
#ifndef ZLISP_VECTOR_H
#define ZLISP_VECTOR_H

#include "check.h"
#include "status.h"
#include <memory.h>
#include <stdlib.h>

#define VECTOR_CAPACITY_STEP 10

#define INITIALIZE_VECTOR(VECTOR, T)                                           \
	typedef struct VECTOR {                                                    \
		T*     data;                                                           \
		size_t length;                                                         \
		size_t capacity;                                                       \
	} VECTOR;                                                                  \
                                                                               \
	static struct VECTOR make_##VECTOR() {                                     \
		return (struct VECTOR){                                                \
		    .data     = malloc(VECTOR_CAPACITY_STEP * sizeof(T)),              \
		    .length   = 0,                                                     \
		    .capacity = VECTOR_CAPACITY_STEP,                                  \
		};                                                                     \
	}                                                                          \
                                                                               \
	static enum status clean_##VECTOR(struct VECTOR* v)                        \
	{                                                                          \
		if (v->data) {                                                         \
			free(v->data);                                                     \
		}                                                                      \
		v->data     = NULL;                                                    \
		v->length   = 0;                                                       \
		v->capacity = 0;                                                       \
		return OK;                                                             \
	}                                                                          \
                                                                               \
	static int is_##VECTOR##_full(const struct VECTOR* v)                      \
	{                                                                          \
		return v->length == v->capacity;                                       \
	}                                                                          \
                                                                               \
	static enum status extend_##VECTOR(struct VECTOR* v, size_t new_capacity)  \
	{                                                                          \
		if (v->capacity >= new_capacity) {                                     \
			return OK;                                                         \
		}                                                                      \
		T* new = reallocarray(v->data, new_capacity, sizeof(T));               \
		if (new) {                                                             \
			v->data     = new;                                                 \
			v->capacity = new_capacity;                                        \
			return OK;                                                         \
		} else {                                                               \
			return ERR_NO_MEM;                                                 \
		}                                                                      \
	}                                                                          \
                                                                               \
	static enum status resize_##VECTOR(struct VECTOR* v, size_t new_size)      \
	{                                                                          \
		if (v->capacity < new_size) {                                          \
			CHECK_OK(extend_##VECTOR(v, new_size));                            \
		}                                                                      \
		v->length = new_size;                                                  \
		return OK;                                                             \
	}                                                                          \
                                                                               \
	static enum status VECTOR##_push(struct VECTOR* v, T element)              \
	{                                                                          \
		if (is_##VECTOR##_full(v)) {                                           \
			CHECK_OK(extend_##VECTOR(v, v->capacity + VECTOR_CAPACITY_STEP));  \
		}                                                                      \
		v->data[v->length] = element;                                          \
		v->length++;                                                           \
		return OK;                                                             \
	}                                                                          \
                                                                               \
	static enum status VECTOR##_get(struct VECTOR* v,                          \
	                                size_t         index,                      \
	                                T*             element)                    \
	{                                                                          \
		if (index >= v->length) {                                              \
			return ERR_INDEX_OUT_OF_BOUND;                                     \
		}                                                                      \
		*element = v->data[index];                                             \
		return OK;                                                             \
	}                                                                          \
                                                                               \
	static enum status VECTOR##_set(struct VECTOR* v, size_t index, T element) \
	{                                                                          \
		if (index >= v->length) {                                              \
			return ERR_INDEX_OUT_OF_BOUND;                                     \
		}                                                                      \
		v->data[index] = element;                                              \
		return OK;                                                             \
	}                                                                          \
                                                                               \
	static enum status VECTOR##_reset(struct VECTOR* v)                        \
	{                                                                          \
		CHECK_OK(clean_##VECTOR(v));                                           \
		*v = make_##VECTOR();                                                  \
		return OK;                                                             \
	}

#endif  // ZLISP_VECTOR_H

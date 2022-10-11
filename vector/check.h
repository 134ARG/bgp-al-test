//
// Created by fort64 on 7/9/2021.
//

#ifndef ZLISP_CHECK_H
#define ZLISP_CHECK_H

#include "../logger/logger.h"
#include <error.h>

#define CHECK_OK(status)                                                       \
	do {                                                                       \
		if (status != OK) {                                                    \
			LOG_ERROR("Operation Failed. errno: %d", errno);                   \
			return status;                                                     \
		}                                                                      \
	} while (0)

#define CHECK(cond, err_code)                                                  \
	do {                                                                       \
		if (!(cond)) {                                                         \
			LOG_ERROR("Operation Failed. errno: %d", errno);                   \
			return err_code;                                                   \
		}                                                                      \
	} while (0)

#endif  // ZLISP_CHECK_H

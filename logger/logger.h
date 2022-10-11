//
// Created by fort64 on 7/9/2021.
//

#ifndef ZLISP_LOGGER_H
#define ZLISP_LOGGER_H

#include <stdarg.h>
#include <stdio.h>

enum log_level {
	LDEBUG = 0,
	LINFO,
	LWARN,
	LERROR,
};

void set_log_level(enum log_level level);

void
logger(unsigned int level, const char* file, int line, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));

#define LOG_ERROR(fmt, ...)                                                    \
	logger(LERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) logger(LWARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) logger(LINFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...)                                                    \
	logger(LDEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif  // ZLISP_LOGGER_H

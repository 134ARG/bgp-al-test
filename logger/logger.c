#include "logger.h"

static const unsigned int levels = 4;

static const char* log_levels[4] = {"[DEBUG]", "[INFO]", "[WARN]", "[ERROR]"};

static unsigned int current_log_level = 1;

void
set_log_level(enum log_level level)
{
	current_log_level = level;
}

void
logger(unsigned int level, const char* file, int line, const char* fmt, ...)
{
	if (level < current_log_level) {
		return;
	}

	if (level >= levels) {
		LOG_ERROR("invalid level num");
		return;
	}

	va_list args;
	va_start(args, fmt);

	fprintf(stderr, "%s: %s:%d: ", log_levels[level], file, line);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");

	va_end(args);
}
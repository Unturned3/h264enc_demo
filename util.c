
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>

#include "conf.h"
#include "util.h"

static char DLOG_LEVEL = DLOG_DEFAULT_LEVEL_CHAR;
#define DLOG_FP(level) (level >= DLOG_ERR_CHAR ? stderr : stdout)

void dlog(const char *format, ...) {
#ifdef ENABLE_DLOG
	va_list args;
	va_start(args, format);
	if (strnlen(format, DLOG_MAX_LEN) >= 2) {
		if (format[0] == DLOG_SOH_CHAR) {
			if (format[1] >= DLOG_LEVEL)
				vfprintf(DLOG_FP(format[1]), format + 2, args);
			goto done;
		}
	}
	// No level specified; defaulting to DLOG_INFO
	if (DLOG_INFO_CHAR >= DLOG_LEVEL)
		vfprintf(stdout, format, args);
done:
	va_end(args);
#endif
}

void dlog_set_level(char *level) {
	dlog(DLOG_INFO "Info: DLOG_LEVEL changed to %c\n", level[1]);
	DLOG_LEVEL = level[1];
}

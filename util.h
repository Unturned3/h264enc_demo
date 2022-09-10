
#ifndef _util_h_
#define _util_h_

#include <stdarg.h>
#include <string.h>
#include <stdint.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

#define perror_ret(x, y) do{ if ((x)<0) {perror(y); return -1;} }while(0)
#define perror_cleanup(x, y) do{ if ((x)<0) {perror(y); ret = -1; goto cleanup;} }while(0)
//#define dlog_cleanup(x, ...) do{ if ((x)<0) {dlog(__VA_ARGS__); goto cleanup;} }while(0)
#define dlog_cleanup(x, y) do{ if ((x)<0) {dlog(y); ret = -1; goto cleanup;} }while(0)

#define ALIGN_4K(x) (((x) + 4095) & ~4095)
#define ALIGN_1K(x) (((x) + 1023) & ~1023)
#define ALIGN_32B(x) (((x) + 31) & ~31)
#define ALIGN_16B(x) (((x) + 15) & ~15)
#define ALIGN_8B(x) (((x) + 7) & ~7)

// Debug logging function (simple wrapper to fprintf)

#define DLOG_MAX_LEN 512

#define DLOG_SOH "\001"
#define DLOG_DEBUG	DLOG_SOH "1"
#define DLOG_INFO	DLOG_SOH "2"
#define DLOG_WARN	DLOG_SOH "3"
#define DLOG_ERR	DLOG_SOH "4"
#define DLOG_CRIT	DLOG_SOH "5"

#define DLOG_SOH_CHAR	'\001'
#define DLOG_DEBUG_CHAR '1'
#define DLOG_INFO_CHAR	'2'
#define DLOG_WARN_CHAR	'3'
#define DLOG_ERR_CHAR	'4'
#define DLOG_CRIT_CHAR	'5'

#define DLOG_DEFAULT_LEVEL_CHAR    DLOG_INFO_CHAR

void dlog_set_level(char *level);
void dlog(const char *format, ...);

void rt_timer_start();
void rt_timer_stop();
double rt_timer_elapsed();

#endif
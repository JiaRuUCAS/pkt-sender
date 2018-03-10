#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <inttypes.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <string.h>
#include <errno.h>
#include <sys/queue.h>
#include <arpa/inet.h>

/* Log printer: Error level */
#define LOG_ERROR(format, ...) \
		fprintf(stderr, "\033[31m[ERROR]\033[0m %s %d: " format "\n", \
						__FILE__, __LINE__, ##__VA_ARGS__);

/* Log printer: Information level */
#define LOG_INFO(format, ...) \
		fprintf(stdout, "[INFO]  %s %d: " format "\n", \
						__FILE__, __LINE__, ##__VA_ARGS__);

/* Log printer: Warning level */
#define LOG_WARN(format, ...) \
		fprintf(stdout, "[WARN]  %s %d: " format "\n", \
						__FILE__, __LINE__, ##__VA_ARGS__);


/* Log printer: Debug level */
#ifdef PS_DEBUG
#define LOG_DEBUG(format, ...) \
		fprintf(stderr, "[DEBUG] %s %d: " format "\n", \
						__FILE__, __LINE__, ##__VA_ARGS__);
#else
#define LOG_DEBUG(format, ...)
#endif

#define DEBUG_TRACE() \
		LOG_DEBUG("%s", __FUNCTION__)

#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#define MIN(a,b) (((a) < (b)) ? (a) : (b))

/** free a memory and set pointer to NULL */
#define zfree(x) {if (x != NULL) {free(x); x = NULL;}}

/**
 * Convert string to uint8_t
 *
 * @param str
 *	input string
 * @param result
 *	pointer to the result value
 * @return
 * 	- -True on success
 *	- -False on failure
 */
static inline bool str_to_uint8(const char *str, uint8_t *result)
{
	unsigned long val = 0;
	char *end = NULL;

	val = strtoul(str, &end, 0);
	if (errno != 0 || end == str || val > 255) {
		LOG_ERROR("Wrong uint8_t format %s (%d)", str, errno);
		return false;
	}
	*result = (uint8_t)val;
	return true;
}

/** Error code */
enum {
	/** The item is disabled. */
	ERR_DISABLED		= -1,
	/** Out of range */
	ERR_OUT_OF_RANGE	= -2,
	/** Error of DPDK calls */
	ERR_DPDK			= -3,
	/** Error of memory allocation */
	ERR_MEMORY			= -4,
	/** Wrong format */
	ERR_FORMAT			= -5,
	/** Wrong parameters */
	ERR_PARAM			= -6,
	/** Error of file operations */
	ERR_FILE			= -7,
};

#define __BYTES_TO_UINT64(a, b, c, d, e, f, g, h) \
	(((uint64_t)((a) & 0xff) << 56) | \
	((uint64_t)((b) & 0xff) << 48) | \
	((uint64_t)((c) & 0xff) << 40) | \
	((uint64_t)((d) & 0xff) << 32) | \
	((uint64_t)((e) & 0xff) << 24) | \
	((uint64_t)((f) & 0xff) << 16) | \
	((uint64_t)((g) & 0xff) << 8)  | \
	((uint64_t)(h) & 0xff))

/** Convert ETH addr a.b.c.d.e.f to uint64 */
#define ETHADDR(a, b, c, d, e, f) (__BYTES_TO_UINT64(0, 0, a, b, c, d, e, f))

/** Convert a uint64 value to struct eth_addr */
#define ETHADDR_TO_UINT64(addr) __BYTES_TO_UINT64( \
		addr.addr_bytes[0], addr.addr_bytes[1], \
		addr.addr_bytes[2], addr.addr_bytes[3], \
		addr.addr_bytes[4], addr.addr_bytes[5], \
		0, 0)

/** Convert IPv4 addr a.b.c.d to uint32 */
#define IPv4(a, b, c, d)   ((uint32_t)(((a) & 0xff) << 24) |   \
			    (((b) & 0xff) << 16) |	\
			    (((c) & 0xff) << 8)  |	\
			    ((d) & 0xff))

#endif /* _UTIL_H_ */

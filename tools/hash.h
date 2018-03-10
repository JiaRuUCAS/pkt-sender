#ifndef _PKTSENDER_HASH_H_
#define _PKTSENDER_HASH_H_

#include <stdint.h>
#include <unistd.h>

/**
 * Calculate CRC-32C
 *
 * Calculates CRC-32C (a.k.a. CRC-32 Castagnoli) over the data.
 * The polynomial is 0x1edc6f41.
 *
 * @param data
 *	Pointer to data
 * @param data_len
 *	Data length in bytes
 * @param init_val
 *	CRC generator initialization value
 *
 * @return
 *	CRC32C value
 */
uint32_t hash_crc32c(const void *data, uint32_t data_len,
			 uint32_t init_val);


#endif

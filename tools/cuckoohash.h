#ifndef _PKTSENDER_CUCKOO_HASH_H_
#define _PKTSENDER_CUCKOO_HASH_H_

#include <stdint.h>

#include "ringbuffer.h"

/** A hash table structure. */
struct cuckoohash_tbl {
	/** Total table entries. */
	uint32_t entries;
	/** Number of buckets in table. */
	uint32_t num_buckets;
	/** Length of hash key. */
	uint32_t key_len;
	/** Bitmask for getting bucket index from hash signature. */
	uint32_t bucket_bitmask;
	/** Size of each key entry. */
	uint32_t key_entry_size;
	/** Real size of the whole cuckoo hash tbl. */
	uint32_t real_size;
	/** Ring that stores all indexes of the free slots in the key table */
	struct ringbuffer *free_slots;
	/** Table storing all keys and data */
	void *key_store;
	/**
	 * Table with buckets storing all the hash values and key indexes
	 * to the key table
	 */
	struct cuckoohash_bucket *buckets;
};

/**
 * Create a new cuckoo hash table.
 *
 * @param tbl
 *   hash table if successful
 * @param key
 *	size of the key
 * @param entry_nr
 *	max number of entries in the table
 * @return
 *   0 on success
 *   negative value on errors including:
 *    - -EINVAL - invalid parameter passed to function
 *    - -ENOMEM - no appropriate memory area found in which to create memzone
 */
int cuckoohash_create(struct cuckoohash_tbl **tbl,
			uint32_t key, uint32_t entry_nr);

/**
 * De-allocate all memory used by hash table.
 * @param h
 *   Hash table to free
 */
void cuckoohash_destroy(struct cuckoohash_tbl *h);

/**
 * Add a key-value pair to an existing hash table.
 * This operation is not multi-thread safe
 * and should only be called from one thread.
 *
 * @param h
 *   Hash table to add the key to.
 * @param key
 *   Key to add to the hash table.
 * @param data
 *   Data to add to the hash table.
 * @return
 *   - 0 if added successfully
 *   - -EINVAL if the parameters are invalid.
 *   - -ENOSPC if there is no space in the hash for this key.
 */
int cuckoohash_add_key_data(const struct cuckoohash_tbl *h,
			      const void *key, void *data);

/**
 * Remove a key from an existing hash table.
 * This operation is not multi-thread safe
 * and should only be called from one thread.
 *
 * @param h
 *   Hash table to remove the key from.
 * @param key
 *   Key to remove from the hash table.
 * @return
 *   - -EINVAL if the parameters are invalid.
 *   - -ENOENT if the key is not found.
 *   - A positive value that can be used by the caller as an offset into an
 *     array of user data. This value is unique for this key, and is the same
 *     value that was returned when the key was added.
 */
int cuckoohash_del_key(const struct cuckoohash_tbl *h, const void *key);

/**
 * Find a key-value pair in the hash table.
 * This operation is multi-thread safe.
 *
 * @param h
 *   Hash table to look in.
 * @param key
 *   Key to find.
 * @param data
 *   Output with pointer to data returned from the hash table.
 * @return
 *   0 if successful lookup
 *   - EINVAL if the parameters are invalid.
 *   - ENOENT if the key is not found.
 */
int cuckoohash_lookup_data(
	const struct cuckoohash_tbl *h, const void *key, void **data);

/**
 * Iterate through the hash table, returning key-value pairs.
 *
 * @param h
 *   Hash table to iterate
 * @param key
 *   Output containing the key where current iterator
 *   was pointing at
 * @param data
 *   Output containing the data associated with key.
 *   Returns NULL if data was not stored.
 * @param next
 *   Pointer to iterator. Should be 0 to start iterating the hash table.
 *   Iterator is incremented after each call of this function.
 * @return
 *   Position where key was stored, if successful.
 *   - -EINVAL if the parameters are invalid.
 *   - -ENOENT if end of the hash table.
 */
int32_t cuckoohash_iterate(const struct cuckoohash_tbl *h,
				const void **key, void **data, uint32_t *next);
#endif /* _PKTSENDER_CUCKOO_HASH_H_ */

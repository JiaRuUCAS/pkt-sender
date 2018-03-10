#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/queue.h>
#include <sys/mman.h>

#include "cuckoohash.h"
#include "ringbuffer.h"
#include "hash.h"

/* Macro to enable/disable run-time checking of function parameters */
#define RETURN_IF_TRUE(cond, retval) do { \
	if (cond) \
		return retval; \
} while (0)

#define CUCKOO_HASH_INIT_VAL	7

/** Number of items per bucket. */
#define HASH_BUCKET_ENTRIES		4
#define NULL_SIGNATURE			0
#define KEY_ALIGNMENT			16

/** Maximum size of hash table that can be created. */
#define HASH_ENTRIES_MAX        1048576
#define HASH_ENTRIES_MIN		8

/* Structure storing both primary and secondary hashes */
struct cuckoohash_signatures {
	union {
		struct {
			uint32_t current;
			uint32_t alt;
		};
		uint64_t sig;
	};
};

/* Structure that stores key-value pair */
struct cuckoohash_key {
	union {
		uintptr_t idata;
		void *pdata;
	};
	/* Variable key size */
	char key[0];
};

/** Bucket structure */
struct cuckoohash_bucket {
	struct cuckoohash_signatures signatures[HASH_BUCKET_ENTRIES];
	/* Includes dummy key index that always contains index 0 */
	uint32_t key_idx[HASH_BUCKET_ENTRIES + 1];
	uint8_t flag[HASH_BUCKET_ENTRIES];
};

int cuckoohash_create(struct cuckoohash_tbl **tbl,
			uint32_t key, uint32_t entry_nr)
{
	struct cuckoohash_tbl *h;
	struct ringbuffer *r = NULL;

//	void *ptr, *k = NULL;
//	void *buckets = NULL;
	uint32_t i;
	uint32_t bucket_nr, real_size;

	if (key <= 0 || entry_nr <= 0) {
		fprintf(stderr, "invalid values\n");
		*tbl = NULL;
		return -EINVAL;
	}

	if (entry_nr < HASH_ENTRIES_MIN)
		entry_nr = HASH_ENTRIES_MIN;

	bucket_nr = __roundup_2(entry_nr) / HASH_BUCKET_ENTRIES;

	/* | cuckoo structure header: sizeof(struct cuckoohash_tbl) |
	 * | bucket: bucker_nr * sizeof(struct cuckoohash_bucket)   |
	 * | key-value: 
	 *		(entry_nr + 1) * (sizeof(struct cuckoohash_key) + key_len) |
	 */
	real_size = sizeof(struct cuckoohash_tbl)
			+ bucket_nr * sizeof(struct cuckoohash_bucket)
			+ (entry_nr + 1) * (sizeof(struct cuckoohash_key) + key);

	h = (struct cuckoohash_tbl*)mmap(
					NULL, real_size, PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (h == MAP_FAILED) {
		fprintf(stderr, "Failed to allocate memory to cuckoo hash %u, %d\n",
						real_size, errno);
		*tbl = NULL;
		return -ENOMEM;
	}

	memset(h, 0, real_size);
	h->entries = entry_nr;
	h->num_buckets = bucket_nr;
	h->key_len = key;
	h->bucket_bitmask = bucket_nr - 1;
	h->key_entry_size = sizeof(struct cuckoohash_key) + key;
	h->real_size = real_size;
	h->buckets = (struct cuckoohash_bucket *)
					((char *)h + sizeof(struct cuckoohash_tbl));
	h->key_store = (char *)h + sizeof(struct cuckoohash_tbl)
			+ bucket_nr * sizeof(struct cuckoohash_bucket);

	r = ringbuffer_create(NULL, sizeof(uint32_t) * (entry_nr + 1));
	if (r == NULL) {
		munmap(h, real_size);
		*tbl = NULL;
		return -ENOMEM;
	}

	/* populate the free slots ring.
	 * Entry zero is reserved for key misses
	 */
	for (i = 1; i < entry_nr + 1; i++)
		ringbuffer_put(r, (char *)&i, sizeof(uint32_t));
	h->free_slots = r;

	*tbl = h;
	return 0;
}

void cuckoohash_destroy(struct cuckoohash_tbl *h)
{
	if (h == NULL)
		return;

//	fprintf(stdout, "cuckoohash_destroy\n");
	ringbuffer_destroy(h->free_slots);
	munmap(h, h->real_size);
	h = NULL;
}

static uint32_t hash(const struct cuckoohash_tbl *h, const void *key)
{
	/* calc hash result by key */
	return hash_crc32c(key, h->key_len, CUCKOO_HASH_INIT_VAL);
}

/* Calc the secondary hash value from the primary hash value of a given key */
static inline uint32_t hash_secondary(const uint32_t primary_hash)
{
	static const unsigned all_bits_shift = 12;
	static const unsigned alt_bits_xor = 0x5bd1e995;

	uint32_t tag = primary_hash >> all_bits_shift;

	return (primary_hash ^ ((tag + 1) * alt_bits_xor));
}

/* Search for an entry that can be pushed to its alternative location */
static inline int make_space_bucket(const struct cuckoohash_tbl *h,
		  struct cuckoohash_bucket *bkt)
{
	unsigned i, j;
	int ret;
	uint32_t next_bucket_idx;
	struct cuckoohash_bucket *next_bkt[HASH_BUCKET_ENTRIES];

//	printf("make space\n");
	/*
	 * Push existing item (search for bucket with space in
	 * alternative locations) to its alternative location
	 */
	for (i = 0; i < HASH_BUCKET_ENTRIES; i++) {
		/* Search for space in alternative locations */
		next_bucket_idx = bkt->signatures[i].alt & h->bucket_bitmask;
		next_bkt[i] = &h->buckets[next_bucket_idx];
		for (j = 0; j < HASH_BUCKET_ENTRIES; j++) {
			if (next_bkt[i]->signatures[j].sig == NULL_SIGNATURE)
				break;
		}

		if (j != HASH_BUCKET_ENTRIES)
			break;
	}

	/* Alternative location has spare room (end of recursive function) */
	if (i != HASH_BUCKET_ENTRIES) {
		next_bkt[i]->signatures[j].alt = bkt->signatures[i].current;
		next_bkt[i]->signatures[j].current = bkt->signatures[i].alt;
		next_bkt[i]->key_idx[j] = bkt->key_idx[i];
		return i;
	}

	/* Pick entry that has not been pushed yet */
	for (i = 0; i < HASH_BUCKET_ENTRIES; i++)
		if (bkt->flag[i] == 0)
			break;

	/* All entries have been pushed, so entry cannot be added */
	if (i == HASH_BUCKET_ENTRIES)
		return -ENOSPC;

	/* Set flag to indicate that this entry is going to be pushed */
	bkt->flag[i] = 1;
	/* Need room in alternative bucket to insert the pushed entry */
	ret = make_space_bucket(h, next_bkt[i]);
	/*
	 * After recursive function.
	 * Clear flags and insert the pushed entry
	 * in its alternative location if successful,
	 * or return error
	 */
	bkt->flag[i] = 0;
	if (ret >= 0) {
		next_bkt[i]->signatures[ret].alt = bkt->signatures[i].current;
		next_bkt[i]->signatures[ret].current = bkt->signatures[i].alt;
		next_bkt[i]->key_idx[ret] = bkt->key_idx[i];
		return i;
	}

	return ret;
}

static inline int32_t __cuckoohash_add_key_with_hash(
		const struct cuckoohash_tbl *h, const void *key,
		uint32_t sig, void *data)
{
	uint32_t alt_hash;
	uint32_t prim_bucket_idx, sec_bucket_idx;
	unsigned i;
	struct cuckoohash_bucket *prim_bkt, *sec_bkt;
	struct cuckoohash_key *new_k, *k, *keys = h->key_store;
	uint32_t slot_id;
	uint32_t new_idx;
	int ret;

	prim_bucket_idx = sig & h->bucket_bitmask;
	prim_bkt = &h->buckets[prim_bucket_idx];
	__builtin_prefetch((const void *)(uintptr_t)prim_bkt, 0, 3);

	alt_hash = hash_secondary(sig);
	sec_bucket_idx = alt_hash & h->bucket_bitmask;
	sec_bkt = &h->buckets[sec_bucket_idx];
	__builtin_prefetch((const void *)(uintptr_t)sec_bkt, 0, 3);

	/* Get a new slot for storing the new key */
	if (ringbuffer_get(h->free_slots, (char *)&slot_id, sizeof(uint32_t))
					!= sizeof(uint32_t))
		return -ENOSPC;
	new_k = (void *)((uintptr_t)(keys)
			+ (slot_id * h->key_entry_size));
	__builtin_prefetch((const void *)(uintptr_t)new_k, 0, 3);
	new_idx = slot_id;

	/* Check if key is already inserted in primary location */
	for (i = 0; i < HASH_BUCKET_ENTRIES; i++) {
		if (
			prim_bkt->signatures[i].current == sig &&
			prim_bkt->signatures[i].alt == alt_hash)  {
			k = (struct cuckoohash_key *)((char *)keys +
				prim_bkt->key_idx[i] * h->key_entry_size);
			if (memcmp(key, k->key, h->key_len) == 0) {
				ringbuffer_put(h->free_slots, (char *)&slot_id,
								sizeof(uint32_t));
				/* Update data */
				k->pdata = data;
				/*
				 * Return index where key is stored,
				 * substracting the first dummy index
				 */
				return (prim_bkt->key_idx[i] - 1);
			}
		}
	}

	/* Check if key is already inserted in secondary location */
	for (i = 0; i < HASH_BUCKET_ENTRIES; i++) {
		if (
			sec_bkt->signatures[i].alt == sig &&
			sec_bkt->signatures[i].current == alt_hash)  {
			k = (struct cuckoohash_key *)((char *)keys +
				sec_bkt->key_idx[i] * h->key_entry_size);
			if (memcmp(key, k->key, h->key_len) == 0) {
				ringbuffer_put(h->free_slots, (char *)&slot_id,
								sizeof(uint32_t));
				/* Update data */
				k->pdata = data;
				/*
				 * Return index where key is stored,
				 * substracting the first dummy index
				 */
				return (sec_bkt->key_idx[i] - 1);
			}
		}
	}

	/* Copy key */
	memcpy(new_k->key, key, h->key_len);
	new_k->pdata = data;

	/* Insert new entry is there is room in the primary bucket */
	for (i = 0; i < HASH_BUCKET_ENTRIES; i++) {
		/* Check if slot is available */
		if (prim_bkt->signatures[i].sig == NULL_SIGNATURE) {
			prim_bkt->signatures[i].current = sig;
			prim_bkt->signatures[i].alt = alt_hash;
			prim_bkt->key_idx[i] = new_idx;
			return new_idx - 1;
		}
	}

	/* Primary bucket is full, so we need to make space for new entry */
	ret = make_space_bucket(h, prim_bkt);
	/*
	 * After recursive function.
	 * Insert the new entry in the position of the pushed entry
	 * if successful or return error and
	 * store the new slot back in the ring
	 */
	if (ret >= 0) {
		prim_bkt->signatures[ret].current = sig;
		prim_bkt->signatures[ret].alt = alt_hash;
		prim_bkt->key_idx[ret] = new_idx;
		return (new_idx - 1);
	}

	/* Error in addition, store new slot back in the ring */
	ringbuffer_put(
			h->free_slots, (char *)&new_idx, sizeof(uint32_t));
	return ret;
}

int cuckoohash_add_key_data(const struct cuckoohash_tbl *h,
			      const void *key, void *data)
{
	int ret;

	RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);

	ret = __cuckoohash_add_key_with_hash(
			h, key, hash(h, key), data);
	if (ret >= 0)
		return 0;
	return ret;
}

static inline int32_t __cuckoohash_lookup_with_hash(
		const struct cuckoohash_tbl *h, const void *key,
		uint32_t sig, void **data)
{
	uint32_t bucket_idx;
	uint32_t alt_hash;
	unsigned i;
	struct cuckoohash_bucket *bkt;
	struct cuckoohash_key *k, *keys = h->key_store;

	bucket_idx = sig & h->bucket_bitmask;
	bkt = &h->buckets[bucket_idx];

	/* Check if key is in primary location */
	for (i = 0; i < HASH_BUCKET_ENTRIES; i++) {
		if (
			bkt->signatures[i].current == sig &&
			bkt->signatures[i].sig != NULL_SIGNATURE) {
			k = (void *)((uintptr_t)keys +
					(bkt->key_idx[i] * h->key_entry_size));
			if (memcmp(key, k->key, h->key_len) == 0) {
				if (data != NULL)
					*data = k->pdata;
				/*
				 * Return index where key is stored,
				 * substracting the first dummy index
				 */
				return (bkt->key_idx[i] - 1);
			}
		}
	}

	/* Calculate secondary hash */
	alt_hash = hash_secondary(sig);
	bucket_idx = alt_hash & h->bucket_bitmask;
	bkt = &h->buckets[bucket_idx];

	/* Check if key is in secondary location */
	for (i = 0; i < HASH_BUCKET_ENTRIES; i++) {
		if (
			bkt->signatures[i].current == alt_hash &&
			bkt->signatures[i].alt == sig) {
			k = (struct cuckoohash_key *)((char *)keys +
					bkt->key_idx[i] * h->key_entry_size);
			if (memcmp(key, k->key, h->key_len) == 0) {
				if (data != NULL)
					*data = k->pdata;
				/*
				 * Return index where key is stored,
				 * substracting the first dummy index
				 */
				return (bkt->key_idx[i] - 1);
			}
		}
	}

	return -ENOENT;
}

int cuckoohash_lookup_data(
		const struct cuckoohash_tbl *h,
		const void *key, void **data)
{
	RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
	return __cuckoohash_lookup_with_hash(
			h, key, hash(h, key), data);
}

static inline int32_t __cuckoohash_del_key_with_hash(
		const struct cuckoohash_tbl *h,
		const void *key, uint32_t sig)
{
	uint32_t bucket_idx;
	uint32_t alt_hash;
	unsigned i;
	struct cuckoohash_bucket *bkt;
	struct cuckoohash_key *k, *keys = h->key_store;

	bucket_idx = sig & h->bucket_bitmask;
	bkt = &h->buckets[bucket_idx];

	/* Check if key is in primary location */
	for (i = 0; i < HASH_BUCKET_ENTRIES; i++) {
		if (
			bkt->signatures[i].current == sig &&
			bkt->signatures[i].sig != NULL_SIGNATURE) {
			k = (struct cuckoohash_key *)((char *)keys +
					bkt->key_idx[i] * h->key_entry_size);
			if (memcmp(key, k->key, h->key_len) == 0) {
				bkt->signatures[i].sig = NULL_SIGNATURE;
				ringbuffer_put(
					h->free_slots,
					(char *)&(bkt->key_idx[i]),
					sizeof(uint32_t));
				/*
				 * Return index where key is stored,
				 * substracting the first dummy index
				 */
				return (bkt->key_idx[i] - 1);
			}
		}
	}

	/* Calculate secondary hash */
	alt_hash = hash_secondary(sig);
	bucket_idx = alt_hash & h->bucket_bitmask;
	bkt = &h->buckets[bucket_idx];

	/* Check if key is in secondary location */
	for (i = 0; i < HASH_BUCKET_ENTRIES; i++) {
		if (
			bkt->signatures[i].current == alt_hash &&
			bkt->signatures[i].sig != NULL_SIGNATURE) {
			k = (struct cuckoohash_key *)((char *)keys +
					bkt->key_idx[i] * h->key_entry_size);
			if (memcmp(key, k->key, h->key_len) == 0) {
				bkt->signatures[i].sig = NULL_SIGNATURE;
				ringbuffer_put(
					h->free_slots,
					(char *)&(bkt->key_idx[i]),
					sizeof(uint32_t));
				/*
				 * Return index where key is stored,
				 * substracting the first dummy index
				 */
				return (bkt->key_idx[i] - 1);
			}
		}
	}

	return -ENOENT;
}

int cuckoohash_del_key(const struct cuckoohash_tbl *h, const void *key)
{
	RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
	return __cuckoohash_del_key_with_hash(
			h, key, hash(h, key));
}

int32_t cuckoohash_iterate(const struct cuckoohash_tbl *h, const void **key,
								void **data, uint32_t *next)
{
	uint32_t bucket_idx, idx, position;
	struct cuckoohash_key *next_key;
	const uint32_t total_entries = h->num_buckets * HASH_BUCKET_ENTRIES;

	RETURN_IF_TRUE(((h == NULL) || (next == NULL)), -EINVAL);

	/* Out of bounds */
	if (*next >= total_entries)
		return -ENOENT;

	/* Calculate bucket and index of current iterator */
	bucket_idx = *next / HASH_BUCKET_ENTRIES;
	idx = *next % HASH_BUCKET_ENTRIES;

	/* If current position is empty, go to the next one */
	while (h->buckets[bucket_idx].signatures[idx].sig == NULL_SIGNATURE) {
		(*next)++;
		/* End of table */
		if (*next == total_entries)
			return -ENOENT;
		bucket_idx = *next / HASH_BUCKET_ENTRIES;
		idx = *next % HASH_BUCKET_ENTRIES;
	}

	/* Get position of entry in key table */
	position = h->buckets[bucket_idx].key_idx[idx];
	next_key = (struct cuckoohash_key *)((char *)h->key_store +
											position * h->key_entry_size);
	/* Return key and data */
	*key = next_key->key;
	*data = next_key->pdata;

	/* Increment iterator */
	(*next)++;

	return (position - 1);
}

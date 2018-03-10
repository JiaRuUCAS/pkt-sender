#ifndef _PKTSENDER_RING_H_
#define	_PKTSENDER_RING_H_

/** A ring buffer structure */
struct ringbuffer {
	/** If use mmap to allocate memory */
	unsigned int is_mmap;
	/** Total size of the ringbuffer */
	unsigned int size;
	/** Data size of ringbuffer */
	unsigned int data_size;
	/** position of last read */
	unsigned int read_pos;
	/** position of last write */
	unsigned int write_pos;
	/** used for lockless protection */
	unsigned int finish_pos;
	/** the start address of data area */
	char data[0];
};

/**
 * Create a ringbuffer
 *
 * @param file
 *	File of shared memory. If not set, use anonymous mode of mmap.
 * @param size
 *	Total size of data
 * @return
 *	- Pointer to the new ringbuffer on success
 *	- NULL on failure
 */
struct ringbuffer *ringbuffer_create(const char *file, unsigned int size);

/**
 * Open a pre-created ringbuffer
 *
 * This ringbuffer is stored in a shared memory identified by @file
 *
 * @param file
 *	File of shared memory. MUST be set.
 * @param size
 *	Total size of the ringbuffer
 * @return
 *	- Pointer to the opened ringbuffer on success
 *	- NULL on failure
 */
struct ringbuffer *ringbuffer_open(const char *file, unsigned int size);

/**
 * Free all memory related to the ringbuffer
 *
 * @param ring_buf
 *	Pointer to the ringbuffer need to free
 */
void ringbuffer_destroy(struct ringbuffer *ring_buf);

/**
 * Puts some data into the ringbuffer, no locking version
 *
 * This function copies at most @len bytes from the @buffer into
 * the ringbuffer depending on the free space, and returns the number of
 * bytes copied.
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these functions.
 *
 * @param ring_buf
 *	the ringbuffer to be used.
 * @param buffer
 *	the data to be added.
 * @len
 *	the length of the data to be added.
 * @return
 *	the length of the data added
 */
unsigned int ringbuffer_put(struct ringbuffer *ring_buf, 
 	const char *buf, unsigned int len);

/**
 * Get some data from the ringbuffer, no locking version
 *
 * @param ring_buf
 *	the ringbuffer to be used
 * @param buffer
 *	pointer the buffer to store the date get from ringbuffer
 * @param len
 *	length of data need to be getten
 * @return
 *	length of data get from ringbuffer
 */
unsigned int ringbuffer_get(struct ringbuffer *ring_buf,
 		char *buf, unsigned int len);

/**
 * Round up to power of 2
 *
 * @param num
 *	The value to be rounded up
 */
static inline unsigned int __roundup_2(unsigned int num)
{
	num--;

	num |= (num >> 1);
	num |= (num >> 2);
	num |= (num >> 4);
	num |= (num >> 8);
	num |= (num >> 16);
	num++;
	return num;
}

#endif

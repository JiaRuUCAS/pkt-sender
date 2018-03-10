#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>

#include "ringbuffer.h"

static inline __attribute__((const)) int is_power_of_2(unsigned int n)
{
	return (n != 0 && ((n & (n - 1)) == 0));
}

static inline unsigned int __min(unsigned int x, unsigned int y)
{
	return (x > y ? y : x);
}

static inline unsigned int __get_rest_size(unsigned int write_pos,
				unsigned int read_pos, unsigned int size)
{
	unsigned int used = 0;

	if (write_pos == read_pos)
		return size;

	used = (write_pos - read_pos) & (size - 1);
	return (size - used);
}

static inline unsigned int __get_used_size(unsigned int write_pos,
				unsigned int read_pos, unsigned int size)
{
	unsigned int used = 0;

	if (write_pos == read_pos)
		return 0;

	used = (write_pos - read_pos) & (size - 1);
	return used;
}

struct ringbuffer *ringbuffer_create(const char *file, unsigned int size)
{
	struct ringbuffer *ring_buf;
	int fd = 0;
	int real_size = 0;
	int anonymous = 1;

	if (is_power_of_2(size) == 0) {
//		fprintf(stdout, "size %u\n", size);
		size = __roundup_2(size);
//		fprintf(stdout, "roundup to %u\n", size);
	}
//	fprintf(stdout, "data size %d\n", size);
	real_size = size + sizeof(struct ringbuffer);
	
	if (file) {
		anonymous = 0;
	}

	if (!anonymous) {
		fd = open(file, O_CREAT|O_RDWR|O_TRUNC, 00777);
		if (fd < 0) {
			fprintf(stderr, "failed to open file %s\n", file);
			return NULL;
		}

		if (ftruncate(fd, real_size) < 0) {
			fprintf(stderr, "failed to allocate file\n");
			goto remove_fd;
		}
		// mmap
		ring_buf = (struct ringbuffer *)mmap(
						NULL, real_size,
						PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	} else {
		ring_buf = (struct ringbuffer *)mmap(
						NULL, real_size,
						PROT_READ | PROT_WRITE,
						MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	}
	if (ring_buf == MAP_FAILED) {
		fprintf(stderr, "failed to map memory\n");
		goto remove_fd;
	}

	ring_buf->size = real_size;
	ring_buf->data_size = size;
//	ring_buf->data_unit = unit;
	ring_buf->read_pos = 0;
	ring_buf->write_pos = 0;
	ring_buf->finish_pos = 0;

	if (!anonymous)
		close(fd);

	return ring_buf;

remove_fd:
	if (!anonymous) {
		close(fd);
		remove(file);
	}
	return NULL;
}

struct ringbuffer *ringbuffer_open(const char *file, unsigned int size)
{
	struct ringbuffer *ring = NULL;
	int fd;
	int real_size = 0;

	if (file == NULL)
		return NULL;

	real_size = __roundup_2(size) + sizeof(struct ringbuffer);

	fd= open(file, O_RDWR, 00777);
	if (fd < 0) {
		fprintf(stderr, "failed to open file %s\n", file);
		return NULL;
	}

	ring = (struct ringbuffer*)mmap(
					NULL, real_size,
					PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (ring == MAP_FAILED) {
		fprintf(stderr, "failed to map memory\n");
		close(fd);
		return NULL;
	}

	close(fd);
	return ring;
}

void ringbuffer_destroy(struct ringbuffer *ring_buf)
{
//	fprintf(stdout, "ringbuffer destroy\n");
	if (ring_buf) {
		int size = ring_buf->size;
		if (munmap(ring_buf, size) < 0) {
			fprintf(stderr, "failed to ummap\n");
		}
		ring_buf = NULL;
	}
}

/**
 * ringbuffer_put - puts some data into the ringbuffer, no locking version
 * @ring_buf: the ringbuffer to be used.
 * @buffer: the data to be added.
 * @len: the length of the data to be added.
 *
 * This function copies at most @len bytes from the @buffer into
 * the ringbuffer depending on the free space, and returns the number of
 * bytes copied.
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these functions.
 */
unsigned int ringbuffer_put(struct ringbuffer *ring_buf,
		const char *buffer, unsigned int len)
{   
	unsigned int l, write_old, write_new;

	while(1) {
		write_old = ring_buf->write_pos;
		l = __get_rest_size(write_old, ring_buf->read_pos, ring_buf->data_size);
		if (l <= len) {
			fprintf(stderr, "ring: ring is full, drop item\n");
			return 0;
		}
		write_new = (write_old + len) & (ring_buf->data_size - 1);
		if (__sync_bool_compare_and_swap(&(ring_buf->write_pos), write_old, write_new))
			break;
	}

	/* first put the data starting from write_pos to buffer end */
	l = __min(len, ring_buf->data_size - (write_old & (ring_buf->data_size - 1)));
//	fprintf(stdout, "write at write_pos %d, len %d\n", ring_buf->write_pos, l); fflush(stdout);
	if (l > 0)
		memcpy(ring_buf->data + (write_old & (ring_buf->data_size - 1)), buffer, l);

	/* then put the rest (if any) at the beginning of the buffer */
	if (len > l)
		memcpy(ring_buf->data, buffer + l, len - l);

	while(1) {
		if (__sync_bool_compare_and_swap(&(ring_buf->finish_pos), write_old, write_new))
			break;
	}
//	fprintf(stdout, "ring: put %u to %u\n", write_old, write_new);

	return len;
}

/**
 *  ringbuffer_get - gets some data from the ringbuffer, no locking version
 *  @ring_buf: the ringbuffer to be used.
 *  @buffer: where the data must be copied.
 *  @len: the size of the destination buffer.
 * 
 *  This function copies at most @len bytes from the ringbuffer into the
 *  @buffer and returns the number of copied bytes.
 * 
 *  Note that with only one concurrent reader and one concurrent
 *  writer, you don't need extra locking to use these functions.
 */
unsigned int ringbuffer_get(struct ringbuffer *ring_buf,
		char *buffer, unsigned int len)
{
	unsigned int l = 0, size = 0, write_local, read_old;

	write_local = ring_buf->finish_pos;
	read_old = ring_buf->read_pos;
	size = __min(len, __get_used_size(write_local, read_old, ring_buf->data_size));

	if (size == 0)
		return 0;

	/* first get the data from ring_buf->read_pos until the end of the buffer */
	l = __min(size, ring_buf->data_size - (read_old & (ring_buf->data_size - 1)));
	if (l > 0)
		memcpy(buffer, ring_buf->data + (read_old & (ring_buf->data_size - 1)), l);

	/* then get the rest (if any) from the beginning of the buffer */
	if (size > l)
		memcpy(buffer + l, ring_buf->data, size - l);

	ring_buf->read_pos = (read_old + size) & (ring_buf->data_size - 1);
//	fprintf(stdout, "ring: read %u to %u\n", read_old, ring_buf->read_pos);
//	fprintf(stdout, "ring: get %u, write_pos %u, read_pos %u, size %u\n",
//					size, write_local, read_old, ring_buf->data_size);
//	fflush(stdout);
	return size;
}

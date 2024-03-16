#define _GNU_SOURCE // for assert_perror
#include <assert.h>

#include "ringbuf.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

// slab_size must be greater than 1.
void init_shared_ringbuf(struct shared_rbuf *rb, size_t slab_size) {
	assert(slab_size > 1);

	*rb = (struct shared_rbuf) {.closed = false, .head = 0, .tail = 0, .size = slab_size};

	pthread_mutexattr_t m_attr;
	pthread_mutexattr_init(&m_attr);
	pthread_mutexattr_setpshared(&m_attr, PTHREAD_PROCESS_SHARED);
	
	assert_perror(pthread_mutex_init(&rb->mtx, &m_attr));

	pthread_condattr_t cond_attr;
	pthread_condattr_init(&cond_attr);
	pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);

	assert_perror(pthread_cond_init(&rb->has_data, &cond_attr));
	assert_perror(pthread_cond_init(&rb->has_space, &cond_attr));
}

// Requires that rb->mtx is unlocked, and no threads wait on rb->has_data or rb->has_space.
void destroy_ringbuf(struct shared_rbuf *rb) {
	assert_perror(pthread_mutex_destroy(&rb->mtx));
	assert_perror(pthread_cond_destroy(&rb->has_data));
	assert_perror(pthread_cond_destroy(&rb->has_space));
}

// --- Internal utility functions that require that the mutex is locked ---

static bool is_empty(const struct shared_rbuf *rb) {
	return rb->head == rb->tail;
}

static size_t ringbuf_capacity(const struct shared_rbuf *rb) {
	// we can't use all the capacity of the buffer at a moment
	// because then empty and full states would be indistinguishable (head = tail in both cases)
	// now, if head == tail, buffer is empty, and if head == (tail + 1) % size, buffer is full
    return rb->size - 1;
}

static size_t ringbuf_bytes_used(const struct shared_rbuf *rb) {
	if (rb->head <= rb->tail)
		return rb->tail - rb->head;
	else
		return rb->size - (rb->head - rb->tail);
}

static size_t ringbuf_bytes_free(const struct shared_rbuf *rb) {
	return ringbuf_capacity(rb) - ringbuf_bytes_used(rb);
}

// --- User-facing functions that lock the ringbuffer ---

// Reads at most len bytes from the ring buffer into buf, removing them from the ringbuffer.
// If ringbuffer is empty, blocks until there is data available.
// Returns length of written buffer (always greater than zero).
// Returns zero if there is no more data pending.
size_t pop_ringbuf(struct ringbuf *rb, char *buf, size_t len) {
	struct shared_rbuf *srb = rb->srb;
	assert_perror(pthread_mutex_lock(&srb->mtx));

	while (is_empty(srb) && !srb->closed) {
		assert_perror(pthread_cond_wait(&srb->has_data, &srb->mtx));
	}

	if (srb->head == srb->tail && srb->closed) {
		assert_perror(pthread_mutex_unlock(&srb->mtx));
		return 0;
	}

	len = min(len, ringbuf_bytes_used(srb)); // can't copy more than there is available

	size_t to_copy = min(len, srb->size - srb->head);
	memcpy(buf, rb->slab + srb->head, to_copy);
	srb->head = (srb->head + to_copy) % srb->size;

	if (to_copy < len) {
		// rollover to the beginning of ring buffer
		size_t more_to_copy = len - to_copy;
		memcpy(buf + to_copy, rb->slab, more_to_copy);
		srb->head = (srb->head + more_to_copy) % srb->size;
	}

	assert_perror(pthread_cond_signal(&srb->has_space));
	assert_perror(pthread_mutex_unlock(&srb->mtx));

	return len;
}

// Blocks until there is space in the ring buffer to write len bytes.
// If len is greater than capacity of the ring buffer (see `get_ringbuf_capacity`), this blocks forever.
void push_ringbuf(struct ringbuf *rb, const char *data, const size_t len) {
	struct shared_rbuf *srb = rb->srb;
	assert_perror(pthread_mutex_lock(&srb->mtx));

	while (ringbuf_bytes_free(srb) < len) {
		pthread_cond_wait(&srb->has_space, &srb->mtx);
	};

	size_t to_copy = min(len, srb->size - srb->tail);
	memcpy(rb->slab + srb->tail, data, to_copy);
	srb->tail = (srb->tail + to_copy) % srb->size;

	if (to_copy < len) {
		size_t more_to_copy = len - to_copy;
		memcpy(rb->slab, data + to_copy, more_to_copy);
		srb->tail = (srb->tail + more_to_copy) % srb->size;
	}

	assert_perror(pthread_cond_signal(&srb->has_data));
	assert_perror(pthread_mutex_unlock(&srb->mtx));
}

void close_ringbuf(struct shared_rbuf *rb) {
	assert_perror(pthread_mutex_lock(&rb->mtx));
	rb->closed = true;

	// notify waiting thread that ringbuffer is closed
	assert_perror(pthread_cond_signal(&rb->has_data));
	assert_perror(pthread_mutex_unlock(&rb->mtx));
}

// Returns the capacity of the ring buffer by acquiring internal mutex.
size_t get_ringbuf_capacity(struct ringbuf *rb) {
	assert_perror(pthread_mutex_lock(&rb->srb->mtx));
	size_t res = ringbuf_capacity(rb->srb);
	assert_perror(pthread_mutex_unlock(&rb->srb->mtx));

	return res;
}

// --- Read and write threads for the given ring buffer ---

static const size_t MAX_IO_BUFSIZE = 512;

static size_t get_io_bufsize(struct ringbuf *rb) {
	return min(get_ringbuf_capacity(rb), MAX_IO_BUFSIZE);
}

// reads data from stdin and pushes it to the ring buffer
void *push_stdin(void *arg) {
	struct ringbuf *rb = arg;
	// we can't push more than the capacity of the ring buffer at once
	char buf[get_io_bufsize(rb)];

	while (true) {
		// wait for data to be available
		size_t bytes_read = fread(buf, 1, sizeof(buf), stdin);
		if (bytes_read == 0) {
			if (feof(stdin)) break;

			else {
				// we should only have either EOF or error
				assert(ferror(stdin));
				fprintf(stderr, "Error reading from stdin\n");
				exit(EXIT_FAILURE);
			}
		}

		push_ringbuf(rb, buf, bytes_read);
	}

	close_ringbuf(rb->srb);

	return NULL;
}

// pops data from the given ring buffer and writes it to stdout
void *pop_stdout(void *arg) {
	struct ringbuf *rb = arg;
	char buf[get_io_bufsize(rb)];

	size_t buf_len;
	while ((buf_len = pop_ringbuf(rb, buf, sizeof(buf))) != 0) {
		assert(buf_len <= sizeof(buf));
		fwrite(buf, 1, buf_len, stdout);
	}

	destroy_ringbuf(rb->srb);

	return NULL;
}
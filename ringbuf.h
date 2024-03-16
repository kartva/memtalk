#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdbool.h>
#include <stdlib.h>

// Metadata for a shared ring buffer that exists in shared memory.
// Must be initialized with init_shared_ringbuf, and destroyed with destroy_ringbuf.
struct shared_rbuf {
	pthread_mutex_t mtx; // controls access to the ring buffer
	pthread_cond_t has_data; // notifies the reader that there is data available
	pthread_cond_t has_space; // notifies the writer that there is space available

	bool closed;
	int head;
	int tail;
	size_t size; // size of buf in bytes; use `ringbuf_capacity()` for capacity
};

struct ringbuf {
	struct shared_rbuf *srb;
	char *slab; // this is stored outside shared memory
	            // since each process has its own address space
};

// buf_size must be greater than 1.
void init_shared_ringbuf(struct shared_rbuf *rb, size_t buf_size);

// Requires that rb->mtx is unlocked, and no threads wait on rb->has_data or rb->has_space.
void destroy_ringbuf(struct shared_rbuf *rb);

// Reads at most len bytes from the ring buffer into buf, removing them from the ringbuffer.
// If ringbuffer is empty, blocks until there is data available.
// Returns length of written buffer (always greater than zero).
// Returns zero if there is no more data pending.
size_t pop_ringbuf(struct ringbuf *rb, char *buf, size_t len);

// Blocks until there is space in the ring buffer to write len bytes.
// If len is greater than capacity of the ring buffer (see `get_ringbuf_capacity`), this blocks forever.
void push_ringbuf(struct ringbuf *rb, const char *data, const size_t len);

void close_ringbuf(struct shared_rbuf *rb);

// Returns the capacity of the ring buffer by acquiring internal mutex.
size_t get_ringbuf_capacity(struct ringbuf *rb);

// --- Read and write threads for the ring buffer ---

// reads data from stdin and pushes it to the passed ring buffer
void *push_stdin(void *arg);

// pops data from the passed ring buffer and writes it to stdout
void *pop_stdout(void *arg);

#endif // RINGBUF_H
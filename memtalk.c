#define _GNU_SOURCE // for assert_perror
#include <assert.h>

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/mman.h>        /* For shm_open, mmap, shm_unlink */
#include <sys/stat.h>        /* For open permssion (S_*) constants */
#include <fcntl.h>           /* For open mode (O_*) constants */

#include "magic.h"
#include "ringbuf.h"
#include "utils.h"

char *shm_name = NULL;
bool shm_creator_process;

int shm_fd = -1;
void *shm = MAP_FAILED;
size_t shm_size = 0;

void cleanup() {
	if (shm != MAP_FAILED) {
		debug("unmapping shared memory\n");
		if (munmap(shm, shm_size) == -1) assert_perror(errno);
	}

	if (shm_fd != -1) {
		debug("closing shared memory file descriptor\n");
		if (close(shm_fd) == -1) assert_perror(errno);
	}

	if (shm_name != NULL && shm_creator_process) {
		debug("unlinking shared memory\n");
		if (shm_unlink(shm_name) == -1) {
			if (errno == ENOENT) debug("shared memory already unlinked\n");
			else assert_perror(errno);
		}
	}
}

int main (int argc, char *argv[]) {
	atexit(cleanup); // called on return from main or exit()

	signal(SIGTERM, exit);
	signal(SIGINT, exit);

	if (argc != 3 || strcmp(argv[1], "-f") != 0) {
		fprintf(stderr, "Usage: %s -f <shm_name>\n", argv[0]);
		return -1;
	}

	shm_name = argv[2];
	shm_creator_process = true;
	shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR,
	                            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

	if (errno == EINVAL) {
		fprintf(stderr, "Invalid shm name\n");
		return -1;
	}

	if (errno == EEXIST) {
		// shm has been created already
		shm_creator_process = false;
		shm_fd = shm_open(shm_name, O_RDWR, 0);
	}

	// ensure our fd is valid (and no unexpected errors occured)
	succeeds(shm_fd);
	debug("shared memory file descriptor: %d\n", shm_fd);

	// --- Calculate object sizes  ---
	shm_size = sysconf(_SC_PAGESIZE);

	const size_t metadata_size =
			sizeof(magic_t) + 2  * (sizeof(struct shared_rbuf) + alignof(struct shared_rbuf));

	const size_t min_slab_size =
			2 /* two slabs */ * (2 /* each slab must be at least two bytes */);

	if (metadata_size + min_slab_size >= shm_size) {
		fprintf(stderr, "Not enough memory in page (%zu bytes) for ringbuffers (requires %zu bytes)\n",
		        shm_size, metadata_size + min_slab_size);
		return -1;
	}

	const size_t ringbuffer_slab_size = (shm_size - metadata_size) / 2;

	// --- Mmap shared memory ---
	// both creator and opener processes set the size of the shared memory
	// since the opener could access the memory before the creator has initialized it
	succeeds(ftruncate(shm_fd, shm_size));

	shm = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	succeeds(shm == MAP_FAILED ? -1 : 0);

	debug("shared memory mapped at %p\n", shm);

	// --- Map pointers into shared memory ---
	magic_t *magic_ptr = shm;

	struct shared_rbuf *shared_ringbuffer_1 =
			next_aligned_ptr(magic_ptr + 1, alignof(struct shared_rbuf));

	char *slab_1 = (char *)(shared_ringbuffer_1 + 1);

	struct shared_rbuf *shared_ringbuffer_2 =
			next_aligned_ptr(slab_1 + ringbuffer_slab_size, alignof(struct shared_rbuf));

	char *slab_2 = (char *)(shared_ringbuffer_2 + 1);

	assert((char *)slab_2 + ringbuffer_slab_size <= (char *)shm + shm_size);

	// --- Initialize ring buffers ---

	struct ringbuf push_rb, pop_rb;
	if (shm_creator_process) {
		debug("initializing ringbuffers\n");

		init_shared_ringbuf(shared_ringbuffer_1, ringbuffer_slab_size);
		init_shared_ringbuf(shared_ringbuffer_2, ringbuffer_slab_size);

		write_magic(magic_ptr);

		push_rb = (struct ringbuf) {.srb = shared_ringbuffer_1, .slab = slab_1};
		pop_rb  = (struct ringbuf) {.srb = shared_ringbuffer_2, .slab = slab_2};
	} else {
		block_on_magic(magic_ptr);

		push_rb = (struct ringbuf) {.srb = shared_ringbuffer_2, .slab = slab_2};
		pop_rb  = (struct ringbuf) {.srb = shared_ringbuffer_1, .slab = slab_1};
	}

	// --- Start threads ---

	pthread_t push_stdin_thd, pop_stdout_thd;

	assert_perror(pthread_create(&push_stdin_thd, NULL, push_stdin, &push_rb));
	assert_perror(pthread_create(&pop_stdout_thd, NULL, pop_stdout, &pop_rb));
	assert_perror(pthread_join(push_stdin_thd, NULL));
	assert_perror(pthread_join(pop_stdout_thd, NULL));

	return 0;
}
#include "utils.h"
#include "magic.h"

#include <errno.h>
#include <linux/futex.h>     /* Definition of FUTEX_* constants */
#include <stdbool.h>
#include <stdint.h>
#include <sys/syscall.h>     /* Definition of SYS_* constants */
#include <unistd.h>

const magic_t MAGIC_VAL = 0x12345678;

// Blocks until the futex at magic_ptr has the magic value.
void block_on_magic(magic_t *magic_ptr) {
	debug("waiting for magic number\n");
	while (true) {
    	int futex_ret = syscall(SYS_futex, magic_ptr, FUTEX_WAIT,
	                                   0 /* sleep if *addr == 0 */, NULL, NULL, 0);

    	if (futex_ret == 0 || (futex_ret == -1 && errno == EAGAIN)) {
			if (__atomic_load_n(magic_ptr, __ATOMIC_SEQ_CST) == MAGIC_VAL) {
				debug("magic number read, starting\n");
				return;
			}
		} else {
			succeeds(futex_ret);
		}
	}
}

// Attempts to wake futex waiting on magic_ptr.
void write_magic(magic_t *magic_ptr) {
	__atomic_store_n(magic_ptr, MAGIC_VAL, __ATOMIC_SEQ_CST);
	debug("magic value written\n");
	succeeds(syscall(SYS_futex, magic_ptr, FUTEX_WAKE,
	                            1 /* wake a single waiter*/, NULL, NULL, 0));
}
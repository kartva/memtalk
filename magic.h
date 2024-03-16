#ifndef MAGIC_H
#define MAGIC_H

#include <stdint.h>

// Futexes operate on 32-bit integers.
typedef uint32_t magic_t;

extern const magic_t MAGIC_VAL;

// Blocks until the futex at magic_ptr has the magic value.
void block_on_magic(magic_t *magic_ptr);

// Attempts to wake futex waiting on magic_ptr.
void write_magic(magic_t *magic_ptr);

#endif // MAGIC_H
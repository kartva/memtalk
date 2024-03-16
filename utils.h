#ifndef UTILS_H
#define UTILS_H

#define _GNU_SOURCE // for assert_perror
#include <assert.h>

#include <stdlib.h>

#define min(a, b) ((a) < (b) ? (a) : (b))

// if expr evaluates to -1, print an error message using errno and exits.
#define succeeds(expr) do { if ((expr) == -1) { assert_perror(errno); exit(EXIT_FAILURE); } } while (0)

void debug(const char *msg, ...);

// aligns ptr to the next multiple of align
void *next_aligned_ptr(const void *ptr, size_t align);

#endif // UTILS_H
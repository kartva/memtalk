#include "utils.h"

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

void debug(const char *msg, ...) {
	#ifdef DEBUG
	va_list args;
	va_start(args, msg);
	vfprintf(stderr, msg, args);
	va_end(args);
	#endif
}

// aligns ptr to the next multiple of align
void *next_aligned_ptr(const void *ptr, size_t align) {
	return (void *)((((uintptr_t)ptr + align - 1) / align) * align);
}
#pragma once

#include <stddef.h>

/* write(1, buf, len) through the newlib-style system call 64. */
void bm_write(const char *buf, size_t len);

/*
 * Minimal printf for micro-test and the benchmarks: %s %c %d %u %x %p %%
 * with the l and ll length modifiers. No division is used, so the 64-bit
 * conversions need neither libgcc nor a multiply.
 */
int bm_printf(const char *fmt, ...);

/*
 * Same conversions into a buffer. Output is cut at cap - 1 characters and
 * always NUL-terminated when cap > 0; the return value is the length the full
 * output would have, as with snprintf.
 */
int bm_snprintf(char *out, size_t cap, const char *fmt, ...);

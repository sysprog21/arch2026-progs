#pragma once

#include <stddef.h>
#include <stdint.h>

/* Freestanding runtime: GCC emits calls to these even with -ffreestanding. */
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

/* RV32I has no multiply; GCC calls this for '*'. */
uint32_t __mulsi3(uint32_t a, uint32_t b);

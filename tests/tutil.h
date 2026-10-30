#pragma once

#include <stdint.h>

/* Helpers shared by the test files. Everything here is RV32I-safe C. */

/* xorshift32: the pseudo-random source of every generated vector. */
static inline uint32_t xs32(uint32_t x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

/* A shift-and-add hash of a word sequence (no multiply). tools/oracle/gen.py
 * computes the same value for the golden tables in *vec.h. */
#define HASH_INIT 0x9E3779B9u

static inline uint32_t hash_add(uint32_t h, uint32_t v)
{
    h += v;
    h += h << 10;
    h ^= h >> 6;
    return h;
}

static inline uint32_t hash_fin(uint32_t h)
{
    h += h << 3;
    h ^= h >> 11;
    h += h << 15;
    return h;
}

/* Restoring division, one quotient bit per step: the reference for tests that
 * need a quotient or a remainder without trusting any code under test. */
static inline uint32_t ref_udivmod(uint32_t a, uint32_t b, uint32_t *rem)
{
    uint32_t q = 0, r = 0;
    for (int i = 31; i >= 0; i--) {
        uint32_t carry = r >> 31; /* the shift below would drop this bit */
        r = (r << 1) | ((a >> i) & 1u);
        if (carry || r >= b) {
            r -= b;
            q |= 1u << i;
        }
    }
    *rem = r;
    return q;
}

/* Euclid's algorithm on top of ref_udivmod: the reference GCD. */
static inline uint32_t ref_gcd(uint32_t a, uint32_t b)
{
    while (b) {
        uint32_t r;
        ref_udivmod(a, b, &r);
        a = b;
        b = r;
    }
    return a;
}

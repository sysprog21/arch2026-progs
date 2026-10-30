#pragma once

#include <stdint.h>

/*
 * Arithmetic built from logic, and Q11.20 fixed point (Quiz 3 Problem C).
 *
 * op1(a, b, n): a + b modulo 2^32 using only bitwise operators and a loop that
 *   runs until the carry word is zero; *n receives the number of loop passes.
 *   Each pass computes c = (a & b) << 1, a = a ^ b, b = c. op1(x, 0, n) returns
 *   x with *n = 0; op1(0xFFFF, 1, n) returns 0x10000 with *n = 17.
 * op2(a, b): a - b modulo 2^32, built from op1 as op1(a, op1(~b, 1)).
 * op3(a, b): floor((a + b) / 2) of the exact sum, with no intermediate
 * overflow. q_mul(a, b): Q11.20 product. A stored value A represents A / 2^20.
 * The product is formed in 64 bits, 2^19 (half a unit) is added, and the sum is
 *   shifted right arithmetically by 20, so ties round toward plus infinity
 *   (q_mul(3, 524288) = 2, q_mul(-3, 524288) = -1). The result keeps the low 32
 *   bits: a product outside the format wraps, it does not saturate.
 *
 * The C versions carry the plain names and the hand-written RV32I versions an
 * _asm suffix. Both must agree on every input. Production code replaces op1
 * and op2 with add and sub; the bitwise forms stay as the references the
 * tests exercise.
 */
#define Q 20

uint32_t op1(uint32_t a, uint32_t b, int *n);
uint32_t op2(uint32_t a, uint32_t b);
uint32_t op3(uint32_t a, uint32_t b);
int32_t q_mul(int32_t a, int32_t b);

#ifdef __riscv
uint32_t op1_asm(uint32_t a, uint32_t b, int *n);
uint32_t op2_asm(uint32_t a, uint32_t b);
uint32_t op3_asm(uint32_t a, uint32_t b);
int32_t q_mul_asm(int32_t a, int32_t b);
#endif

void fixed_register_tests(void);

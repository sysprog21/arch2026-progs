#pragma once

#include <stdint.h>

/* BFloat16 (Quiz 1 Problem B): 1 sign bit, 8 exponent bits (bias 127), 7
 * fraction bits, carried as the 16-bit pattern. No float is ever used in
 * arithmetic; a binary32 value travels as its uint32_t bit pattern.
 *
 * Special values: +0 is 0x0000, -0 is 0x8000, +Inf is 0x7F80, -Inf is 0xFF80,
 * and the quiet NaN this code produces is 0x7FC0. An exponent field of 0 with a
 * nonzero fraction is a denormal.
 *
 * bf16_from_f32(f32bits): narrow a binary32 pattern.
 *   * Exponent field 0xFF (Inf and NaN): return the top 16 bits unchanged.
 *   * Otherwise add 0x7FFF plus bit 16 of the input and keep the top 16 bits:
 *     round to nearest, ties to even. This may carry into the exponent and
 *     turn the largest finite float into Inf; denormals narrow like any other
 *     value (0x00010000 gives 0x0001).
 *
 * bf16_mul(a, b): sign is the XOR of the signs.
 *   * The operands are examined in the order a, then b. If a has exponent
 *     0xFF: a NaN a is returned unchanged; otherwise Inf times 0 (b a zero
 *     with any sign) is the quiet NaN; otherwise the result is Inf with the
 *     product sign, whatever b is, a NaN b included. Only then is b examined
 *     the same way: a NaN b is returned unchanged, Inf times 0 is the quiet
 *     NaN, otherwise Inf with the product sign.
 *   * Zero times a finite value is zero with the product sign.
 *   * Denormal operands are normalized first (the leading one is shifted up
 *     and the exponent adjusted). The 8-bit by 8-bit significand product is
 *     renormalized by one bit when it reaches bit 15, and the fraction is
 *     TRUNCATED (round toward zero), not rounded.
 *   * Result exponent >= 0xFF gives Inf. Result exponent <= 0 gives a
 *     denormal by shifting the fraction right (1 - exponent) bits, but when
 *     the exponent is below -6 the result is zero with the product sign.
 *
 * bf16_sqrt(a):
 *   * A NaN is returned unchanged. +Inf is returned unchanged. -Inf, and any
 *     negative nonzero value, give the quiet NaN.
 *   * Any zero (including -0) gives +0 (0x0000). Denormals give +0.
 *   * Otherwise sqrt(2^e * m) = 2^(e/2) * sqrt(m): an odd exponent is made
 *     even by doubling the significand, and the integer square root of the
 *     scaled significand is found by a search over mid in [90, 256] for the
 *     largest mid with (mid * mid) / 128 <= m (truncating). The result is
 *     renormalized into [128, 256), the exponent follows, and an exponent
 *     >= 0xFF gives Inf, <= 0 gives +0. The result is NOT correctly rounded:
 *     the worst relative error over the positive normals is 0.3884%.
 *
 * The C versions carry the plain names and the hand-written RV32I versions an
 * _asm suffix. Both must agree on every input; neither may use mul, div, rem or
 * a libgcc routine except the __mulsi3 the runtime supplies.
 */
uint16_t bf16_from_f32(uint32_t f32bits);
uint16_t bf16_mul(uint16_t a, uint16_t b);
uint16_t bf16_sqrt(uint16_t a);

#ifdef __riscv
uint16_t bf16_from_f32_asm(uint32_t f32bits);
uint16_t bf16_mul_asm(uint16_t a, uint16_t b);
uint16_t bf16_sqrt_asm(uint16_t a);
#endif

/* Instruction budgets: the most instructions one call may retire, for any
 * operands (tests/tbf16.c measures them with the instret counter). The assembly
 * is the deliverable and gets the tight budget, which admits a shift-and-add
 * multiply; the C version is the step on the way and only has to stay within
 * the loose one.
 */
#define BF16_F32_INSN_BUDGET 60u
#define BF16_MUL_INSN_BUDGET 400u
#define BF16_SQRT_INSN_BUDGET 1500u
#define BF16_C_F32_INSN_BUDGET 150u
#define BF16_C_MUL_INSN_BUDGET 1000u
#define BF16_C_SQRT_INSN_BUDGET 4000u

void bf16_register_tests(void);

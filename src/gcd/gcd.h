#pragma once

#include <stdint.h>

/* Binary GCD without a divider (Quiz 2 Problem A).
 *
 * gcd_u32(a, b) is the greatest common divisor of two unsigned 32-bit values,
 * with gcd(x, 0) = gcd(0, x) = x and so gcd(0, 0) = 0. The C version (gcd_u32)
 * and the hand-written RV32I version (gcd_u32_asm) must agree on every input.
 * Neither may use mul, div or rem, a libgcc routine, or a loop that subtracts
 * one operand from the other one step at a time.
 */
uint32_t gcd_u32(uint32_t a, uint32_t b);

#ifdef __riscv
uint32_t gcd_u32_asm(uint32_t a, uint32_t b);
#endif

/* Instruction budgets: the most instructions one gcd_u32 call may retire, for
 * any operands (tests/tgcd.c measures the worst cases with the instret
 * counter). The assembly is the deliverable and gets the tight budget; the C
 * version is the step on the way and only has to finish within the loose one.
 * Both admit the binary algorithm; the loose one still rules out a loop that
 * subtracts one operand from the other step by step.
 */
#define GCD_INSN_BUDGET 1500u
#define GCD_C_INSN_BUDGET 4000u

void gcd_register_tests(void);

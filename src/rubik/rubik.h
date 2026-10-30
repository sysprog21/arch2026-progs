#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Optimal solver for the 2x2x2 Rubik's Cube (Homework 1, ported).
 *
 * A state is 14 ASCII characters, PPPPPPPOOOOOOO: seven cubie digits '1'..'7'
 * (a permutation) followed by seven orientation digits '1'..'3' whose values
 * minus one sum to a multiple of 3. "12345671111111" is the solved cube. The
 * model is the one of sysprog21/minirubik solver.c: three faces R, B, D with
 * quarter turns, and the nine moves
 *
 *   R R2 R'  B B2 B'  D D2 D'
 *
 * rubik_solve(state, out, cap) writes a shortest solution as move names
 * separated by single spaces ("R2 D B2 R' D R' D B'") into out, NUL-terminated,
 * and returns the number of moves (0 for the solved cube, which writes ""). The
 * length is the exact distance: no solution is accepted merely for working.
 * It returns
 *   -1  when state is not a valid 14-character state: wrong length, a digit
 *       out of range, a repeated cubie, or orientations that do not sum to a
 *       multiple of 3; out is left untouched in that case;
 *   -2  when the solution and its NUL do not fit in cap bytes; out[0] is set
 *       to '\0' if cap is at least 1.
 * The search runs on the target: a precomputed table of all 3,674,160 states
 * is not accepted, and the total static data stays within 128 KiB.
 *
 * The C version is rubik_solve and the hand-written RV32I version is
 * rubik_solve_asm. Both must return the same length for every state.
 */
#define RUBIK_STATE_LEN 14

int rubik_solve(const char *state, char *out, size_t cap);

#ifdef __riscv
int rubik_solve_asm(const char *state, char *out, size_t cap);
#endif

void rubik_register_tests(void);

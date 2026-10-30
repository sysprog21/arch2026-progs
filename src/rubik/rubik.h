#pragma once

#include <stddef.h>
#include <stdint.h>

/* Optimal solver for the 2x2x2 Rubik's Cube (Homework 1, ported).
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
 * length is the exact distance: no solution is accepted merely for working. It
 * returns
 *   -1  when state is not a valid 14-character state: wrong length, a digit
 *       out of range, a repeated cubie, or orientations that do not sum to a
 *       multiple of 3; out is left untouched in that case;
 *   -2  when the solution and its NUL do not fit in cap bytes; out[0] is set
 *       to '\0' if cap is at least 1.
 * The search runs on the target: a precomputed table of all 3,674,160 states is
 * not accepted. Budgets enforce it, and the plain breadth-first search of
 * sysprog21/minirubik (18 MB of tables, about 10^9 instructions) fails them:
 *
 *   RUBIK_DATA_BUDGET    every allocated section of each implementation, code
 *                        included, in bytes (scripts/driver.py: size -A)
 *   RUBIK_INSN_BUDGET    instructions one rubik_solve_asm call may retire, for
 *                        any state, the 2,644 states at distance 11 included
 *   RUBIK_C_INSN_BUDGET  the same for rubik_solve, which is the step on the
 *                        way and has the looser budget of the assignment: no
 *                        state at distance 11 above 5 * 10^7 instructions
 *
 * tests/trubik.c counts the instructions with the instret counter. The tight
 * budget is the level of weiso131/minirubik, an iterative deepening search that
 * ends in a binary-search table of the states within 5 moves and a table of the
 * states at distance 11 (67 KB of data). Its C version retires at most 7.5
 * million instructions for the most expensive states (-O1: 7.5, -O2: 7.0, -O3:
 * 5.5, -Os: 7.4 million, found by counting the work of all 3,674,160 states and
 * measuring the worst 40 on rv32emu), and its assembly less. A plain iterative
 * deepening A* with the permutation and orientation distances as the heuristic
 * needs about 10 million for a typical state at distance 11: good enough for
 * the C version, not for the assembly.
 *
 * The C version is rubik_solve and the hand-written RV32I version is
 * rubik_solve_asm. Both must return the same length for every state.
 */
#define RUBIK_STATE_LEN 14
#define RUBIK_DATA_BUDGET (128 * 1024)
#define RUBIK_INSN_BUDGET 7500000u
#define RUBIK_C_INSN_BUDGET 50000000u

int rubik_solve(const char *state, char *out, size_t cap);

#ifdef __riscv
int rubik_solve_asm(const char *state, char *out, size_t cap);
#endif

void rubik_register_tests(void);

#include <stddef.h>
#include "rubikvec.h"
#include "src/rubik/rubik.h"
#include "tperf.h"
#include "tutil.h"
#include "utest.h"

typedef int (*solve_fn)(const char *, char *, size_t);

/* The cube model of sysprog21/minirubik solver.c, written out again here so the
 * checks never trust the solver: each destination cubie takes the cubie and the
 * twist of source[face][destination].
 */
enum { CUBIES = 7 };

struct cube {
    uint8_t p[CUBIES], o[CUBIES];
};

static const uint8_t source[3][CUBIES] = {
    {1, 4, 2, 0, 3, 5, 6},
    {0, 1, 2, 4, 5, 6, 3},
    {0, 2, 5, 3, 1, 4, 6},
};
static const uint8_t twist[3][CUBIES] = {
    {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0},
};
static const char *const move_name[9] = {"R",  "R2", "R'", "B", "B2",
                                         "B'", "D",  "D2", "D'"};

static struct cube parse(const char *s)
{
    struct cube c;
    for (int i = 0; i < CUBIES; i++) {
        c.p[i] = (uint8_t) (s[i] - '1');
        c.o[i] = (uint8_t) (s[i + CUBIES] - '1');
    }
    return c;
}

/* No division in the tests: the face and the turn count of each move, and the
 * orientation sum modulo 3 (at most 2 + 2), come from tables.
 */
static const uint8_t face_of[9] = {0, 0, 0, 1, 1, 1, 2, 2, 2};
static const uint8_t turns_of[9] = {1, 2, 3, 1, 2, 3, 1, 2, 3};
static const uint8_t mod3[5] = {0, 1, 2, 0, 1};

static struct cube turn(struct cube c, int move)
{
    int face = face_of[move];
    for (int t = 0; t < turns_of[move]; t++) {
        struct cube n;
        for (int i = 0; i < CUBIES; i++) {
            int from = source[face][i];
            n.p[i] = c.p[from];
            n.o[i] = mod3[c.o[from] + twist[face][i]];
        }
        c = n;
    }
    return c;
}

static int solved(const struct cube *c)
{
    for (int i = 0; i < CUBIES; i++) {
        if (c->p[i] != i || c->o[i] != 0)
            return 0;
    }
    return 1;
}

/* Returns the move index for the token at s (length len), or -1. */
static int token(const char *s, size_t len)
{
    for (int m = 0; m < 9; m++) {
        size_t k = 0;
        while (move_name[m][k] && k < len && s[k] == move_name[m][k])
            k++;
        if (!move_name[m][k] && k == len)
            return m;
    }
    return -1;
}

/* Solve `state`, then check the text: single-space separated valid tokens, no
 * leading or trailing space, as many tokens as the return value, and applying
 * them reaches the solved cube.
 *
 * Returns the number of moves, or a negative value after printing the reason.
 */
static int solve_and_verify(solve_fn solve, const char *state, int *moves_out)
{
    char out[96];
    for (size_t i = 0; i < sizeof out; i++)
        out[i] = '#';
#ifdef __riscv
    uint64_t used;
    int n;
    PERF_CALL(used, n = solve(state, out, sizeof out));
#else
    int n = solve(state, out, sizeof out);
#endif
#ifdef __riscv
    unsigned budget =
        solve == rubik_solve_asm ? RUBIK_INSN_BUDGET : RUBIK_C_INSN_BUDGET;
    if (used > budget)
        UTEST_FAILED("solve(%s) retired %llu instructions, the budget is %u\n",
                     state, used, budget);
#endif
    if (n < 0)
        UTEST_FAILED("solve(%s) returned %d\n", state, n);
    struct cube c = parse(state);
    size_t len = 0;
    while (len < sizeof out && out[len])
        len++;
    if (len == sizeof out)
        UTEST_FAILED("solve(%s): output is not terminated\n", state);
    int count = 0;
    size_t i = 0;
    while (i < len) {
        size_t j = i;
        while (j < len && out[j] != ' ')
            j++;
        int m = token(out + i, j - i);
        if (m < 0)
            UTEST_FAILED("solve(%s): bad move text \"%s\"\n", state, out);
        c = turn(c, m);
        count++;
        if (j < len && j + 1 == len)
            UTEST_FAILED("solve(%s): trailing space in \"%s\"\n", state, out);
        i = j + 1;
    }
    if (count != n)
        UTEST_FAILED("solve(%s) returned %d for \"%s\"\n", state, n, out);
    if (!solved(&c))
        UTEST_FAILED("solve(%s): \"%s\" does not solve the cube\n", state, out);
    *moves_out = n;
    return TEST_PASS;
}

/* The vectors of one distance: each solution solves the cube, its length is the
 * exact distance, and it is found within the instruction budget. The twelve
 * depths are separate tests, so a slow solver shows where it gives up.
 */
static int check_depth(solve_fn solve, int depth)
{
    int seen = 0;
    for (size_t i = 0; i < RUBIK_NCASES; i++) {
        if (rubik_cases[i].distance != depth)
            continue;
        int n;
        int r = solve_and_verify(solve, rubik_cases[i].state, &n);
        if (r != TEST_PASS)
            return r;
        if (n != depth)
            UTEST_FAILED("solve(%s) used %d moves, the distance is %d\n",
                         rubik_cases[i].state, n, depth);
        seen++;
    }
    ASSERT_TRUE(seen > 0);
    return TEST_PASS;
}

static int check_solved(solve_fn solve)
{
    char out[8] = "####";
    ASSERT_EQ(solve("12345671111111", out, sizeof out), 0);
    ASSERT_EQ(out[0], '\0');
    char one[1] = {'#'};
    ASSERT_EQ(solve("12345671111111", one, 1), 0);
    ASSERT_EQ(one[0], '\0');
    return TEST_PASS;
}

/* Each of the nine moves applied to the solved cube is exactly one move from
 * solved: the state after R is solved by R', and so on.
 */
static int check_single_moves(solve_fn solve)
{
    for (int m = 0; m < 9; m++) {
        struct cube c = turn(parse("12345671111111"), m);
        char state[RUBIK_STATE_LEN + 1];
        for (int i = 0; i < CUBIES; i++) {
            state[i] = (char) ('1' + c.p[i]);
            state[i + CUBIES] = (char) ('1' + c.o[i]);
        }
        state[RUBIK_STATE_LEN] = '\0';
        int n;
        int r = solve_and_verify(solve, state, &n);
        if (r != TEST_PASS)
            return r;
        ASSERT_EQ(n, 1);
    }
    return TEST_PASS;
}

/* Follow an 11-move solution: after its first move the cube is at distance 10,
 * then 9, down to 0, so the solver is consistent with itself.
 */
static int check_chain(solve_fn solve)
{
    char state[RUBIK_STATE_LEN + 1] = "21345671111111";
    int expect = 11;
    for (;;) {
        char out[96];
        int n = solve(state, out, sizeof out);
        ASSERT_EQ(n, expect);
        if (n == 0)
            break;
        size_t len = 0;
        while (out[len] && out[len] != ' ')
            len++;
        struct cube c = turn(parse(state), token(out, len));
        for (int i = 0; i < CUBIES; i++) {
            state[i] = (char) ('1' + c.p[i]);
            state[i + CUBIES] = (char) ('1' + c.o[i]);
        }
        expect--;
    }
    return TEST_PASS;
}

/* Rejection paths: wrong length, digit out of range, repeated cubie, bad
 * orientation sum, trailing garbage. The output buffer stays untouched.
 */
static int check_invalid(solve_fn solve)
{
    for (size_t i = 0; i < RUBIK_NINVALID; i++) {
        char out[16];
        for (size_t k = 0; k < sizeof out; k++)
            out[k] = '#';
        ASSERT_EQ(solve(rubik_invalid[i], out, sizeof out), -1);
        for (size_t k = 0; k < sizeof out; k++)
            ASSERT_EQ(out[k], '#');
    }
    return TEST_PASS;
}

/* A buffer one byte short fails with -2 and an empty string, the exact size
 * succeeds, and a zero-length buffer is never written.
 */
static int check_small_buffer(solve_fn solve)
{
    const char *state = "21345671111111";
    char big[96];
    int n = solve(state, big, sizeof big);
    ASSERT_EQ(n, 11);
    size_t len = 0;
    while (big[len])
        len++;
    char out[96];
    for (size_t cap = 1; cap <= len; cap++) {
        for (size_t k = 0; k < sizeof out; k++)
            out[k] = '#';
        ASSERT_EQ(solve(state, out, cap), -2);
        ASSERT_EQ(out[0], '\0');
    }
    for (size_t k = 0; k < sizeof out; k++)
        out[k] = '#';
    ASSERT_EQ(solve(state, out, len + 1), 11);
    ASSERT_MEM_EQ(out, big, len + 1);
    ASSERT_EQ(out[len + 1], '#');
    out[0] = '#';
    ASSERT_EQ(solve(state, out, 0), -2);
    ASSERT_EQ(out[0], '#');
    out[0] = '#';
    ASSERT_EQ(solve("12345671111111", out, 0), -2);
    ASSERT_EQ(out[0], '#');
    return TEST_PASS;
}

#define DUAL(name)                        \
    static int name##_c(void)             \
    {                                     \
        return check_##name(rubik_solve); \
    }                                     \
    DUAL_ASM(name)

#ifdef __riscv
#define DUAL_ASM(name)                        \
    static int name##_asm(void)               \
    {                                         \
        return check_##name(rubik_solve_asm); \
    }
#define REG(name)                                                  \
    do {                                                           \
        register_test(name##_c, NULL, NULL, "rubik_c_" #name);     \
        register_test(name##_asm, NULL, NULL, "rubik_asm_" #name); \
    } while (0)
#else
#define DUAL_ASM(name)
#define REG(name) register_test(name##_c, NULL, NULL, "rubik_c_" #name)
#endif

/* One test per distance, for the C and the assembly version. */
#ifdef __riscv
#define DEPTH(d)                                \
    static int depth##d##_c(void)               \
    {                                           \
        return check_depth(rubik_solve, d);     \
    }                                           \
    static int depth##d##_asm(void)             \
    {                                           \
        return check_depth(rubik_solve_asm, d); \
    }
#define REG_DEPTH(d)                                                     \
    do {                                                                 \
        register_test(depth##d##_c, NULL, NULL, "rubik_c_depth" #d);     \
        register_test(depth##d##_asm, NULL, NULL, "rubik_asm_depth" #d); \
    } while (0)
#else
#define DEPTH(d)                            \
    static int depth##d##_c(void)           \
    {                                       \
        return check_depth(rubik_solve, d); \
    }
#define REG_DEPTH(d) register_test(depth##d##_c, NULL, NULL, "rubik_c_depth" #d)
#endif

DEPTH(0)
DEPTH(1)
DEPTH(2)
DEPTH(3)
DEPTH(4)
DEPTH(5)
DEPTH(6)
DEPTH(7)
DEPTH(8)
DEPTH(9)
DEPTH(10)
DEPTH(11)

DUAL(solved)
DUAL(single_moves)
DUAL(chain)
DUAL(invalid)
DUAL(small_buffer)

#ifdef __riscv
/* Both versions return the same length on every vector. */
static int asm_matches_c(void)
{
    char a[96], b[96];
    for (size_t i = 0; i < RUBIK_NCASES; i++) {
        int na = rubik_solve_asm(rubik_cases[i].state, a, sizeof a);
        int nb = rubik_solve(rubik_cases[i].state, b, sizeof b);
        ASSERT_EQ(na, nb);
    }
    return TEST_PASS;
}
#endif

void rubik_register_tests(void)
{
    REG_DEPTH(0);
    REG_DEPTH(1);
    REG_DEPTH(2);
    REG_DEPTH(3);
    REG_DEPTH(4);
    REG_DEPTH(5);
    REG_DEPTH(6);
    REG_DEPTH(7);
    REG_DEPTH(8);
    REG_DEPTH(9);
    REG_DEPTH(10);
    REG_DEPTH(11);
    REG(solved);
    REG(single_moves);
    REG(chain);
    REG(invalid);
    REG(small_buffer);
#ifdef __riscv
    register_test(asm_matches_c, NULL, NULL, "rubik_asm_matches_c");
#endif
}

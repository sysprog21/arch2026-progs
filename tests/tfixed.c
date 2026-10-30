#include <stddef.h>
#include "fixedvec.h"
#include "src/fixed/fixed.h"
#include "tperf.h"
#include "tutil.h"
#include "utest.h"

struct impl {
    uint32_t (*op1)(uint32_t, uint32_t, int *);
    uint32_t (*op2)(uint32_t, uint32_t);
    uint32_t (*op3)(uint32_t, uint32_t);
    int32_t (*q_mul)(int32_t, int32_t);
};

/* Independent Q11.20 product: the exact signed 64-bit product from 16-bit
 * partial products of the magnitudes, plus 2^19, then a floor shift by 20,
 * truncated to 32 bits. It shares no step with the routine under test.
 */
static int32_t ref_qmul(int32_t a, int32_t b)
{
    uint32_t ma = a < 0 ? 0u - (uint32_t) a : (uint32_t) a;
    uint32_t mb = b < 0 ? 0u - (uint32_t) b : (uint32_t) b;
    uint32_t al = ma & 0xFFFF, ah = ma >> 16, bl = mb & 0xFFFF, bh = mb >> 16;
    uint64_t mag = (uint64_t) (al * bl) + ((uint64_t) (al * bh) << 16) +
                   ((uint64_t) (ah * bl) << 16) + ((uint64_t) (ah * bh) << 32);
    int64_t p = (int64_t) ((a < 0) != (b < 0) ? 0u - mag : mag);
    p += (int64_t) 1 << 19;
    return (int32_t) (p >> 20);
}

/* op1: the bitwise adder */

static int check_op1_cases(const struct impl *m)
{
    for (int i = 0; i < OP1_NCASES; i++) {
        const uint32_t *c = &op1_cases[4 * i];
        int n = -1;
        uint32_t s = m->op1(c[0], c[1], &n);
        if (s != c[2] || n != (int) c[3])
            UTEST_FAILED(
                "op1(0x%x, 0x%x) = 0x%x in %d passes, expected 0x%x in %u\n",
                c[0], c[1], s, n, c[2], c[3]);
    }
    return TEST_PASS;
}

/* 64 x 64 operands, one golden hash per row over (sum, pass count). */
static int check_op1_grid(const struct impl *m)
{
    for (int i = 0; i < 64; i++) {
        uint32_t h = HASH_INIT;
        for (int j = 0; j < 64; j++) {
            int n = -1;
            uint32_t s = m->op1(op_pts[i], op_pts[j], &n);
            h = hash_add(hash_add(h, s), (uint32_t) n);
        }
        if (hash_fin(h) != op1_row_hash[i])
            UTEST_FAILED("op1 grid row %d (a = 0x%x) differs\n", i, op_pts[i]);
    }
    return TEST_PASS;
}

static int check_op1_random(const struct impl *m)
{
    uint32_t x = 0x0DDBA11u;
    for (int blk = 0; blk < 8; blk++) {
        uint32_t h = HASH_INIT;
        for (int k = 0; k < 128; k++) {
            x = xs32(x);
            uint32_t a = x;
            x = xs32(x);
            int n = -1;
            uint32_t s = m->op1(a, x, &n);
            h = hash_add(hash_add(h, s), (uint32_t) n);
        }
        if (hash_fin(h) != op1_rnd_hash[blk])
            UTEST_FAILED("op1 random block %d differs\n", blk);
    }
    return TEST_PASS;
}

/* The sum is a + b, the pass count is symmetric, zero only when b is zero and
 * never above 32; adding zero or a number to itself is predictable.
 */
static int check_op1_laws(const struct impl *m)
{
    uint32_t x = 0xFACEFEEDu;
    for (int k = 0; k < 4096; k++) {
        x = xs32(x);
        uint32_t a = x, b = k & 1 ? x >> (k & 15) : xs32(x);
        int n = -1, n2 = -1;
        uint32_t s = m->op1(a, b, &n), s2 = m->op1(b, a, &n2);
        ASSERT_UNSIGNED_EQ(s, a + b);
        ASSERT_UNSIGNED_EQ(s2, s);
        ASSERT_EQ(n, n2);
        ASSERT_TRUE(n >= 0 && n <= 32);
        ASSERT_EQ(n == 0, b == 0);
    }
    return TEST_PASS;
}

/* op2: subtraction */

static int check_sub(const struct impl *m)
{
    ASSERT_UNSIGNED_EQ(m->op2(0, 0), 0);
    ASSERT_UNSIGNED_EQ(m->op2(0, 1), 0xFFFFFFFFu);
    ASSERT_UNSIGNED_EQ(m->op2(1, 0), 1);
    ASSERT_UNSIGNED_EQ(m->op2(0x80000000u, 1), 0x7FFFFFFFu);
    ASSERT_UNSIGNED_EQ(m->op2(0, 0x80000000u), 0x80000000u);
    ASSERT_UNSIGNED_EQ(m->op2(0x7FFFFFFFu, 0xFFFFFFFFu), 0x80000000u);
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++)
            ASSERT_UNSIGNED_EQ(m->op2(op_pts[i], op_pts[j]),
                               op_pts[i] - op_pts[j]);
    }
    uint32_t x = 0xB16B00B5u;
    for (int k = 0; k < 4096; k++) {
        x = xs32(x);
        uint32_t a = x;
        x = xs32(x);
        uint32_t d = m->op2(a, x);
        ASSERT_UNSIGNED_EQ(d, a - x);
        ASSERT_UNSIGNED_EQ(d + x, a);
        ASSERT_UNSIGNED_EQ(m->op2(a, a), 0);
    }
    return TEST_PASS;
}

/* op3: floor of the average without overflow */

static int check_avg(const struct impl *m)
{
    ASSERT_UNSIGNED_EQ(m->op3(0, 0), 0);
    ASSERT_UNSIGNED_EQ(m->op3(1, 0), 0);
    ASSERT_UNSIGNED_EQ(m->op3(1, 1), 1);
    ASSERT_UNSIGNED_EQ(m->op3(1, 2), 1);
    ASSERT_UNSIGNED_EQ(m->op3(3, 4), 3);
    ASSERT_UNSIGNED_EQ(m->op3(0xFFFFFFFFu, 0xFFFFFFFFu), 0xFFFFFFFFu);
    ASSERT_UNSIGNED_EQ(m->op3(0xFFFFFFFFu, 0), 0x7FFFFFFFu);
    ASSERT_UNSIGNED_EQ(m->op3(0xFFFFFFFFu, 1), 0x80000000u);
    ASSERT_UNSIGNED_EQ(m->op3(0x80000000u, 0x80000000u), 0x80000000u);
    ASSERT_UNSIGNED_EQ(m->op3(0xFFFFFFFEu, 0xFFFFFFFFu), 0xFFFFFFFEu);
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            uint64_t want = ((uint64_t) op_pts[i] + op_pts[j]) >> 1;
            ASSERT_UNSIGNED_EQ(m->op3(op_pts[i], op_pts[j]), want);
        }
    }
    uint32_t x = 0xC0DEC0DEu;
    for (int k = 0; k < 4096; k++) {
        x = xs32(x);
        uint32_t a = x;
        x = xs32(x);
        uint64_t want = ((uint64_t) a + x) >> 1;
        ASSERT_UNSIGNED_EQ(m->op3(a, x), want);
        ASSERT_UNSIGNED_EQ(m->op3(x, a), want);
        ASSERT_UNSIGNED_EQ(m->op3(a, a), a);
    }
    return TEST_PASS;
}

/* q_mul */

/* Rounding at ties, sign handling, saturating-looking corners that in fact
 * wrap: expected values come from the instructor's oracle.
 */
static int check_qmul_cases(const struct impl *m)
{
    for (int i = 0; i < QMUL_NCASES; i++) {
        const uint32_t *c = &qmul_cases[3 * i];
        uint32_t got = (uint32_t) m->q_mul((int32_t) c[0], (int32_t) c[1]);
        if (got != c[2])
            UTEST_FAILED("q_mul(0x%x, 0x%x) = 0x%x, expected 0x%x\n", c[0],
                         c[1], got, c[2]);
    }
    return TEST_PASS;
}

/* 64 x 64 interesting operands and 4,096 random pairs against ref_qmul. */
static int check_qmul_reference(const struct impl *m)
{
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            int32_t a = (int32_t) op_pts[i], b = (int32_t) op_pts[j];
            ASSERT_EQ(m->q_mul(a, b), ref_qmul(a, b));
        }
    }
    uint32_t x = 0x5EEDBEEFu;
    for (int k = 0; k < 4096; k++) {
        x = xs32(x);
        int32_t a = (int32_t) x;
        x = xs32(x);
        int32_t b = (int32_t) (k & 1 ? x : x >> (k & 15));
        ASSERT_EQ(m->q_mul(a, b), ref_qmul(a, b));
    }
    return TEST_PASS;
}

/* 1.0 is the identity, 0 annihilates, 2.0 doubles and -1.0 negates (modulo
 * 2^32), the product commutes, and a tie between two stored values rounds up.
 */
static int check_qmul_laws(const struct impl *m)
{
    const int32_t one = 1 << Q;
    uint32_t x = 0x7A57E123u;
    for (int k = 0; k < 2048; k++) {
        x = xs32(x);
        int32_t a = (int32_t) x;
        ASSERT_EQ(m->q_mul(a, one), a);
        ASSERT_EQ(m->q_mul(one, a), a);
        ASSERT_EQ(m->q_mul(a, 0), 0);
        ASSERT_EQ(m->q_mul(0, a), 0);
        ASSERT_UNSIGNED_EQ((uint32_t) m->q_mul(a, 2 * one), (uint32_t) a << 1);
        ASSERT_UNSIGNED_EQ((uint32_t) m->q_mul(a, -one), 0u - (uint32_t) a);
        x = xs32(x);
        ASSERT_EQ(m->q_mul(a, (int32_t) x), m->q_mul((int32_t) x, a));
    }
    /* a * 0.5 is a tie whenever a is odd: it rounds toward plus infinity. */
    static const int8_t half_up[19] = {-4, -4, -3, -3, -2, -2, -1, -1, 0, 0,
                                       1,  1,  2,  2,  3,  3,  4,  4,  5};
    for (int32_t a = -9; a <= 9; a++)
        ASSERT_EQ(m->q_mul(a, one / 2), half_up[a + 9]);
    return TEST_PASS;
}

#ifdef __riscv
struct worst {
    uint64_t insns;
    uint32_t a, b;
};

static void note(struct worst *w, uint64_t used, uint32_t a, uint32_t b)
{
    if (used > w->insns) {
        w->insns = used;
        w->a = a;
        w->b = b;
    }
}

/* The most expensive call of each routine over the 64 x 64 grid and 4,096
 * random pairs has to fit its budget. The figures are printed for tuning.
 */
static int check_perf(const struct impl *m)
{
    struct worst o1 = {0, 0, 0}, o2 = {0, 0, 0}, o3 = {0, 0, 0}, qm = {0, 0, 0};
    uint32_t x = 0x9E3779B9u;
    for (int k = 0; k < 64 * 64 + 4096; k++) {
        uint32_t a, b;
        if (k < 64 * 64) {
            a = op_pts[k >> 6];
            b = op_pts[k & 63];
        } else {
            x = xs32(x);
            a = x;
            x = xs32(x);
            b = x;
        }
        int n;
        uint64_t used;
        PERF_CALL(used, (void) m->op1(a, b, &n));
        note(&o1, used, a, b);
        PERF_CALL(used, (void) m->op2(a, b));
        note(&o2, used, a, b);
        PERF_CALL(used, (void) m->op3(a, b));
        note(&o3, used, a, b);
        PERF_CALL(used, (void) m->q_mul((int32_t) a, (int32_t) b));
        note(&qm, used, a, b);
    }
    int is_asm = m->q_mul == q_mul_asm;
    unsigned b_op1 = is_asm ? FIXED_OP1_INSN_BUDGET : FIXED_C_OP1_INSN_BUDGET;
    unsigned b_op2 = is_asm ? FIXED_OP2_INSN_BUDGET : FIXED_C_OP2_INSN_BUDGET;
    unsigned b_op3 = is_asm ? FIXED_OP3_INSN_BUDGET : FIXED_C_OP3_INSN_BUDGET;
    unsigned b_qmul =
        is_asm ? FIXED_QMUL_INSN_BUDGET : FIXED_C_QMUL_INSN_BUDGET;
    utest_printer("@@perf fixed op1=%llu op2=%llu op3=%llu q_mul=%llu\n",
                  o1.insns, o2.insns, o3.insns, qm.insns);
    if (o1.insns > b_op1)
        UTEST_FAILED(
            "op1(0x%x, 0x%x) retired %llu instructions, the budget is %u\n",
            o1.a, o1.b, o1.insns, b_op1);
    if (o2.insns > b_op2)
        UTEST_FAILED(
            "op2(0x%x, 0x%x) retired %llu instructions, the budget is %u\n",
            o2.a, o2.b, o2.insns, b_op2);
    if (o3.insns > b_op3)
        UTEST_FAILED(
            "op3(0x%x, 0x%x) retired %llu instructions, the budget is %u\n",
            o3.a, o3.b, o3.insns, b_op3);
    if (qm.insns > b_qmul)
        UTEST_FAILED(
            "q_mul(0x%x, 0x%x) retired %llu instructions, the budget is %u\n",
            qm.a, qm.b, qm.insns, b_qmul);
    return TEST_PASS;
}
#endif

#define DUAL(name)                    \
    static int name##_c(void)         \
    {                                 \
        return check_##name(&c_impl); \
    }                                 \
    DUAL_ASM(name)

static const struct impl c_impl = {op1, op2, op3, q_mul};
#ifdef __riscv
static const struct impl asm_impl = {op1_asm, op2_asm, op3_asm, q_mul_asm};
#define DUAL_ASM(name)                  \
    static int name##_asm(void)         \
    {                                   \
        return check_##name(&asm_impl); \
    }
#define REG(name)                                                  \
    do {                                                           \
        register_test(name##_c, NULL, NULL, "fixed_c_" #name);     \
        register_test(name##_asm, NULL, NULL, "fixed_asm_" #name); \
    } while (0)
#else
#define DUAL_ASM(name)
#define REG(name) register_test(name##_c, NULL, NULL, "fixed_c_" #name)
#endif

DUAL(op1_cases)
DUAL(op1_grid)
DUAL(op1_random)
DUAL(op1_laws)
DUAL(sub)
DUAL(avg)
DUAL(qmul_cases)
DUAL(qmul_reference)
DUAL(qmul_laws)
#ifdef __riscv
DUAL(perf)
#endif

#ifdef __riscv
static int asm_matches_c(void)
{
    uint32_t x = 0x0FF1CE00u;
    for (int k = 0; k < 4096; k++) {
        x = xs32(x);
        uint32_t a = x;
        x = xs32(x);
        uint32_t b = x;
        int n1 = -1, n2 = -2;
        ASSERT_UNSIGNED_EQ(op1_asm(a, b, &n1), op1(a, b, &n2));
        ASSERT_EQ(n1, n2);
        ASSERT_UNSIGNED_EQ(op2_asm(a, b), op2(a, b));
        ASSERT_UNSIGNED_EQ(op3_asm(a, b), op3(a, b));
        ASSERT_EQ(q_mul_asm((int32_t) a, (int32_t) b),
                  q_mul((int32_t) a, (int32_t) b));
    }
    return TEST_PASS;
}
#endif

void fixed_register_tests(void)
{
    REG(op1_cases);
    REG(op1_grid);
    REG(op1_random);
    REG(op1_laws);
    REG(sub);
    REG(avg);
    REG(qmul_cases);
    REG(qmul_reference);
    REG(qmul_laws);
#ifdef __riscv
    REG(perf);
#endif
#ifdef __riscv
    register_test(asm_matches_c, NULL, NULL, "fixed_asm_matches_c");
#endif
}

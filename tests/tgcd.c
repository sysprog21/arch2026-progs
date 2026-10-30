#include <stddef.h>
#include "gcdvec.h"
#include "src/gcd/gcd.h"
#include "tperf.h"
#include "tutil.h"
#include "utest.h"

typedef uint32_t (*gcd_fn)(uint32_t, uint32_t);

/* Directed cases: zeros, equal operands, powers of two, coprime and shared
 * factors, 32-bit extremes, consecutive Fibonacci numbers (Euclid's worst
 * case). Expected values come from the instructor's oracle.
 */
static int check_cases(gcd_fn gcd)
{
    for (int i = 0; i < GCD_NCASES; i++) {
        const uint32_t *c = &gcd_cases[3 * i];
        uint32_t got = gcd(c[0], c[1]);
        if (got != c[2])
            UTEST_FAILED("gcd(%u, %u) = %u, expected %u\n", c[0], c[1], got,
                         c[2]);
    }
    return TEST_PASS;
}

/* gcd(x, 0) = gcd(0, x) = gcd(x, x) = x, over bit patterns of every length. */
static int check_identities(gcd_fn gcd)
{
    for (int i = 0; i < 32; i++) {
        uint32_t xs[] = {1u << i, (1u << i) | 1u, (2u << i) - 1u, ~(1u << i)};
        for (size_t k = 0; k < sizeof xs / sizeof xs[0]; k++) {
            uint32_t x = xs[k];
            ASSERT_UNSIGNED_EQ(gcd(x, 0), x);
            ASSERT_UNSIGNED_EQ(gcd(0, x), x);
            ASSERT_UNSIGNED_EQ(gcd(x, x), x);
            ASSERT_UNSIGNED_EQ(gcd(x, 1), 1);
            ASSERT_UNSIGNED_EQ(gcd(1, x), 1);
        }
    }
    ASSERT_UNSIGNED_EQ(gcd(0, 0), 0);
    return TEST_PASS;
}

/* Every pair below 256: 65,536 results against Euclid. */
static int check_small_exhaustive(gcd_fn gcd)
{
    for (uint32_t a = 0; a < 256; a++) {
        for (uint32_t b = 0; b < 256; b++) {
            uint32_t want = ref_gcd(a, b), got = gcd(a, b);
            if (got != want)
                UTEST_FAILED("gcd(%u, %u) = %u, expected %u\n", a, b, got,
                             want);
        }
    }
    return TEST_PASS;
}

/* gcd(2^i, 2^j) = 2^min(i, j) for all 1,024 exponent pairs. */
static int check_powers_of_two(gcd_fn gcd)
{
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            uint32_t want = 1u << (i < j ? i : j);
            ASSERT_UNSIGNED_EQ(gcd(1u << i, 1u << j), want);
        }
    }
    return TEST_PASS;
}

/* Factors of two are stripped from both operands and restored once: gcd(u << i,
 * v << j) = gcd(u, v) << min(i, j) for odd u and v.
 */
static int check_shared_twos(gcd_fn gcd)
{
    uint32_t x = 0x2468ACE1u;
    for (int n = 0; n < 512; n++) {
        x = xs32(x);
        uint32_t u = (x & 0xFFFF) | 1u;
        x = xs32(x);
        uint32_t v = (x & 0xFFFF) | 1u;
        int i = (int) ((x >> 16) & 15), j = (int) ((x >> 20) & 15);
        uint32_t want = ref_gcd(u, v) << (i < j ? i : j);
        ASSERT_UNSIGNED_EQ(gcd(u << i, v << j), want);
    }
    return TEST_PASS;
}

/* Random operands of random bit length against Euclid, plus the algebra:
 * symmetry, g divides both, and the cofactors are coprime.
 */
static int check_random(gcd_fn gcd)
{
    uint32_t x = 0xDEADBEEFu;
    for (int n = 0; n < 2048; n++) {
        x = xs32(x);
        uint32_t a = x >> ((x >> 3) & 31 ? (x >> 3) & 31 : 1);
        x = xs32(x);
        uint32_t b = x >> ((x >> 7) & 31 ? (x >> 7) & 31 : 1);
        uint32_t g = gcd(a, b);
        ASSERT_UNSIGNED_EQ(g, ref_gcd(a, b));
        ASSERT_UNSIGNED_EQ(gcd(b, a), g);
        if (g) {
            uint32_t r, qa = ref_udivmod(a, g, &r);
            ASSERT_UNSIGNED_EQ(r, 0);
            uint32_t qb = ref_udivmod(b, g, &r);
            ASSERT_UNSIGNED_EQ(r, 0);
            ASSERT_UNSIGNED_EQ(ref_gcd(qa, qb), 1);
        }
    }
    return TEST_PASS;
}

/* gcd(F(n), F(m)) = F(gcd(n, m)) for every n, m up to F(47), the largest
 * Fibonacci number below 2^32. Consecutive terms are Euclid's worst case.
 */
static int check_fibonacci(gcd_fn gcd)
{
    uint32_t f[48];
    f[0] = 0;
    f[1] = 1;
    for (int i = 2; i < 48; i++)
        f[i] = f[i - 1] + f[i - 2];
    ASSERT_UNSIGNED_EQ(f[47], 2971215073u);
    for (uint32_t n = 0; n < 48; n++) {
        for (uint32_t m = 0; m < 48; m++)
            ASSERT_UNSIGNED_EQ(gcd(f[n], f[m]), f[ref_gcd(n, m)]);
    }
    return TEST_PASS;
}

#ifdef __riscv
/* Worst cases for the binary algorithm and for Euclid's: extreme operands, a
 * wide gap in size, consecutive Fibonacci numbers, and random pairs. The most
 * expensive call has to fit the budget; the figure is printed for tuning.
 */
static int check_perf(gcd_fn gcd)
{
    static const uint32_t hard[][2] = {
        {0xFFFFFFFFu, 1},           {1, 0xFFFFFFFFu},
        {0x80000000u, 1},           {0x80000000u, 0x7FFFFFFFu},
        {0xFFFFFFFFu, 0xFFFFFFFEu}, {2971215073u, 1836311903u},
        {1836311903u, 1134903170u}, {0xFFFFFFFFu, 0xFFFFu},
        {0xDEADBEEFu, 0xCAFEBABEu}, {0x7FFFFFFFu, 0x40000000u},
        {0xAAAAAAAAu, 0x55555555u}, {0x00010001u, 0xFFFF0000u},
    };
    uint64_t worst = 0;
    uint32_t worst_a = 0, worst_b = 0;
    uint32_t x = 0xFEEDF00Du;
    for (int i = 0; i < 12 + 256; i++) {
        uint32_t a, b;
        if (i < 12) {
            a = hard[i][0];
            b = hard[i][1];
        } else {
            x = xs32(x);
            a = x;
            x = xs32(x);
            b = x;
        }
        uint32_t g;
        uint64_t used;
        PERF_CALL(used, g = gcd(a, b));
        ASSERT_UNSIGNED_EQ(g, ref_gcd(a, b));
        if (used > worst) {
            worst = used;
            worst_a = a;
            worst_b = b;
        }
    }
    unsigned budget = gcd == gcd_u32_asm ? GCD_INSN_BUDGET : GCD_C_INSN_BUDGET;
    utest_printer("@@perf gcd worst=%llu budget=%u at (%u, %u)\n", worst,
                  budget, worst_a, worst_b);
    if (worst > budget)
        UTEST_FAILED(
            "gcd(%u, %u) retired %llu instructions, the budget is %u\n",
            worst_a, worst_b, worst, budget);
    return TEST_PASS;
}
#endif

#define DUAL(name)                    \
    static int name##_c(void)         \
    {                                 \
        return check_##name(gcd_u32); \
    }                                 \
    DUAL_ASM(name)

#ifdef __riscv
#define DUAL_ASM(name)                    \
    static int name##_asm(void)           \
    {                                     \
        return check_##name(gcd_u32_asm); \
    }
#define REG(name)                                                \
    do {                                                         \
        register_test(name##_c, NULL, NULL, "gcd_c_" #name);     \
        register_test(name##_asm, NULL, NULL, "gcd_asm_" #name); \
    } while (0)
#else
#define DUAL_ASM(name)
#define REG(name) register_test(name##_c, NULL, NULL, "gcd_c_" #name)
#endif

DUAL(cases)
DUAL(identities)
DUAL(small_exhaustive)
DUAL(powers_of_two)
DUAL(shared_twos)
DUAL(random)
DUAL(fibonacci)
#ifdef __riscv
DUAL(perf)
#endif

#ifdef __riscv
/* The assembly must equal the C version on operands neither one has seen. */
static int asm_matches_c(void)
{
    uint32_t x = 0x13579BDFu;
    for (int n = 0; n < 4096; n++) {
        x = xs32(x);
        uint32_t a = x;
        x = xs32(x);
        uint32_t b = x >> ((n & 31) ? (n & 31) : 1);
        ASSERT_UNSIGNED_EQ(gcd_u32_asm(a, b), gcd_u32(a, b));
    }
    return TEST_PASS;
}
#endif

void gcd_register_tests(void)
{
    REG(cases);
    REG(identities);
    REG(small_exhaustive);
    REG(powers_of_two);
    REG(shared_twos);
    REG(random);
    REG(fibonacci);
#ifdef __riscv
    REG(perf);
#endif
#ifdef __riscv
    register_test(asm_matches_c, NULL, NULL, "gcd_asm_matches_c");
#endif
}

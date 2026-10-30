#include <stddef.h>
#include "bf16.h"
#include "bf16vec.h"
#include "tutil.h"
#include "utest.h"

struct impl {
    uint16_t (*from_f32)(uint32_t);
    uint16_t (*mul)(uint16_t, uint16_t);
    uint16_t (*sqrt)(uint16_t);
};

#define BF16_NAN 0x7FC0u
#define BF16_INF 0x7F80u

static int is_nan(uint16_t x)
{
    return (x & 0x7F80u) == 0x7F80u && (x & 0x7Fu);
}

/* Independent formulation of the narrowing: compare the discarded half with
 * 0x8000 instead of adding 0x7FFF plus the lowest kept bit. */
static uint16_t ref_from_f32(uint32_t f)
{
    uint32_t hi = f >> 16, lo = f & 0xFFFFu;
    if (((f >> 23) & 0xFF) == 0xFF)
        return (uint16_t) hi;
    if (lo > 0x8000u || (lo == 0x8000u && (hi & 1u)))
        hi++;
    return (uint16_t) hi;
}

/* ---- bf16_from_f32 ---- */

static int check_f32_cases(const struct impl *m)
{
    for (int i = 0; i < BF16_NF32; i++) {
        uint32_t in = bf16_f32_cases[2 * i];
        uint16_t want = (uint16_t) bf16_f32_cases[2 * i + 1];
        uint16_t got = m->from_f32(in);
        if (got != want)
            UTEST_FAILED("from_f32(0x%x) = 0x%x, expected 0x%x\n", in, got,
                         want);
    }
    return TEST_PASS;
}

/* Every discarded half (65,536 values) under 16 characteristic kept halves:
 * normal, odd and even mantissas, the largest finite, denormals, zeros,
 * infinities and NaNs of both signs. Covers both values of bit 16. */
static int check_f32_low_halves(const struct impl *m)
{
    static const uint16_t highs[] = {
        0x3F80, 0x3F81, 0xBF80, 0xBF81, 0x7F7F, 0xFF7F, 0x0000, 0x8000,
        0x0001, 0x0002, 0x007F, 0x0080, 0x7F80, 0xFF80, 0x7FC0, 0xFFC0,
    };
    for (size_t h = 0; h < sizeof highs / sizeof highs[0]; h++) {
        for (uint32_t lo = 0; lo < 65536; lo++) {
            uint32_t in = ((uint32_t) highs[h] << 16) | lo;
            uint16_t got = m->from_f32(in), want = ref_from_f32(in);
            if (got != want)
                UTEST_FAILED("from_f32(0x%x) = 0x%x, expected 0x%x\n", in, got,
                             want);
        }
    }
    return TEST_PASS;
}

/* Every kept half (65,536 values) with the discarded halves that decide
 * rounding: zero, just below a tie, the tie, just above, all ones. */
static int check_f32_high_halves(const struct impl *m)
{
    static const uint16_t lows[] = {0x0000, 0x7FFF, 0x8000, 0x8001, 0xFFFF};
    for (uint32_t hi = 0; hi < 65536; hi++) {
        for (size_t l = 0; l < sizeof lows / sizeof lows[0]; l++) {
            uint32_t in = (hi << 16) | lows[l];
            uint16_t got = m->from_f32(in), want = ref_from_f32(in);
            if (got != want)
                UTEST_FAILED("from_f32(0x%x) = 0x%x, expected 0x%x\n", in, got,
                             want);
        }
    }
    return TEST_PASS;
}

/* ---- bf16_mul ---- */

static int check_mul_cases(const struct impl *m)
{
    for (int i = 0; i < BF16_NMUL; i++) {
        uint16_t a = bf16_mul_cases[3 * i], b = bf16_mul_cases[3 * i + 1];
        uint16_t want = bf16_mul_cases[3 * i + 2], got = m->mul(a, b);
        if (got != want)
            UTEST_FAILED("mul(0x%x, 0x%x) = 0x%x, expected 0x%x\n", a, b, got,
                         want);
    }
    return TEST_PASS;
}

/* 256 x 256 pairs of characteristic operands, one golden hash per row. */
static int check_mul_grid(const struct impl *m)
{
    for (int i = 0; i < 256; i++) {
        uint32_t h = HASH_INIT;
        for (int j = 0; j < 256; j++)
            h = hash_add(h, m->mul(bf16_pts[i], bf16_pts[j]));
        if (hash_fin(h) != bf16_mul_row_hash[i])
            UTEST_FAILED("mul grid row %d (a = 0x%x) differs\n", i,
                         bf16_pts[i]);
    }
    return TEST_PASS;
}

/* All 65,536 values of a against 8 constants, one golden hash per block of
 * 1,024 values of a. */
static int check_mul_columns(const struct impl *m)
{
    for (int c = 0; c < 8; c++) {
        for (int blk = 0; blk < 64; blk++) {
            uint32_t h = HASH_INIT;
            for (uint32_t a = blk * 1024u; a < blk * 1024u + 1024u; a++)
                h = hash_add(h, m->mul((uint16_t) a, bf16_consts[c]));
            if (hash_fin(h) != bf16_mul_col_hash[c * 64 + blk])
                UTEST_FAILED("mul by 0x%x: a in [0x%x, 0x%x) differs\n",
                             bf16_consts[c], blk * 1024u, blk * 1024u + 1024u);
        }
    }
    return TEST_PASS;
}

/* Laws that hold without the oracle: commutativity away from NaN, the sign
 * rule, 1.0 as the identity on normals, zero and infinity handling. */
static int check_mul_laws(const struct impl *m)
{
    for (int i = 0; i < 256; i++) {
        uint16_t a = bf16_pts[i];
        for (int j = 0; j < 256; j++) {
            uint16_t b = bf16_pts[j], p = m->mul(a, b);
            if (is_nan(a) || is_nan(b)) {
                /* a is examined first: a NaN a wins, an infinite a swallows a
                 * NaN b, otherwise the NaN b is returned. */
                uint16_t want =
                    is_nan(a) ? a
                    : (a & 0x7FFF) == BF16_INF
                        ? (uint16_t) (((a ^ b) & 0x8000u) | BF16_INF)
                        : b;
                ASSERT_UNSIGNED_EQ(p, want);
                continue;
            }
            ASSERT_UNSIGNED_EQ(p, m->mul(b, a));
            if (!is_nan(p))
                ASSERT_UNSIGNED_EQ(p >> 15, (a ^ b) >> 15);
            else
                ASSERT_UNSIGNED_EQ(p, BF16_NAN);
        }
        uint32_t e = (a >> 7) & 0xFF;
        if (e >= 1 && e <= 254) {
            ASSERT_UNSIGNED_EQ(m->mul(a, 0x3F80), a);
            ASSERT_UNSIGNED_EQ(m->mul(a, 0xBF80), a ^ 0x8000u);
        }
        if (!is_nan(a) && (a & 0x7FFF) != BF16_INF) {
            ASSERT_UNSIGNED_EQ(m->mul(a, 0x0000), (a & 0x8000u));
            ASSERT_UNSIGNED_EQ(m->mul(a, 0x8000), (a & 0x8000u) ^ 0x8000u);
        }
        if (!is_nan(a) && (a & 0x7FFF) != 0 && (a & 0x7FFF) != BF16_INF) {
            ASSERT_UNSIGNED_EQ(m->mul(a, 0x7F80), (a & 0x8000u) | BF16_INF);
            ASSERT_UNSIGNED_EQ(m->mul(a, 0xFF80), (~a & 0x8000u) | BF16_INF);
        }
    }
    return TEST_PASS;
}

/* ---- bf16_sqrt ---- */

static int check_sqrt_cases(const struct impl *m)
{
    for (int i = 0; i < BF16_NSQRT; i++) {
        uint16_t a = bf16_sqrt_cases[2 * i], want = bf16_sqrt_cases[2 * i + 1];
        uint16_t got = m->sqrt(a);
        if (got != want)
            UTEST_FAILED("sqrt(0x%x) = 0x%x, expected 0x%x\n", a, got, want);
    }
    return TEST_PASS;
}

/* All 65,536 inputs, one golden hash per block of 256. */
static int check_sqrt_all(const struct impl *m)
{
    for (uint32_t blk = 0; blk < 256; blk++) {
        uint32_t h = HASH_INIT;
        for (uint32_t a = blk * 256u; a < blk * 256u + 256u; a++)
            h = hash_add(h, m->sqrt((uint16_t) a));
        if (hash_fin(h) != bf16_sqrt_block_hash[blk])
            UTEST_FAILED("sqrt: inputs [0x%x, 0x%x) differ\n", blk * 256u,
                         blk * 256u + 256u);
    }
    return TEST_PASS;
}

/* Properties over the positive normals: monotone, and the result squared is
 * within 1% of the input (the routine is not correctly rounded; its worst
 * relative error is 0.39%, so the square is within about 0.78%). */
static int check_sqrt_laws(const struct impl *m)
{
    uint16_t prev = 0;
    for (uint32_t x = 0x0080; x <= 0x7F7F; x++) {
        uint16_t r = m->sqrt((uint16_t) x);
        if (r < prev)
            UTEST_FAILED("sqrt is not monotone at 0x%x\n", x);
        prev = r;
        /* x = mx * 2^(ex - 134), r = mr * 2^(er - 134), both mantissas with
         * the hidden one; compare r*r with x after aligning the exponents. */
        if ((r >> 7) == 0 || (r >> 7) >= 0xFF)
            UTEST_FAILED("sqrt(0x%x) = 0x%x is not a positive normal\n", x, r);
        uint32_t mx = 0x80 | (x & 0x7F), mr = 0x80 | (r & 0x7F);
        int ex = (int) (x >> 7), er = (int) (r >> 7);
        int d = 2 * er - ex - 134;
        uint32_t lhs = mr * mr, rhs = mx;
        if (d >= 0)
            lhs <<= d;
        else
            rhs <<= -d;
        uint32_t diff = lhs > rhs ? lhs - rhs : rhs - lhs;
        if (diff * 100u > rhs)
            UTEST_FAILED("sqrt(0x%x) = 0x%x is off by more than 1%%\n", x, r);
    }
    return TEST_PASS;
}

/* Negative inputs, zeros, infinities, NaNs and denormals, by class. */
static int check_sqrt_classes(const struct impl *m)
{
    for (uint32_t x = 0x8001; x < 0x10000; x++) {
        uint16_t got = m->sqrt((uint16_t) x);
        if ((x & 0x7F80) == 0x7F80 && (x & 0x7F))
            ASSERT_UNSIGNED_EQ(got, x);
        else
            ASSERT_UNSIGNED_EQ(got, BF16_NAN);
    }
    for (uint32_t x = 0x0001; x < 0x0080; x++)
        ASSERT_UNSIGNED_EQ(m->sqrt((uint16_t) x), 0);
    for (uint32_t x = 0x7F81; x < 0x8000; x++)
        ASSERT_UNSIGNED_EQ(m->sqrt((uint16_t) x), x);
    ASSERT_UNSIGNED_EQ(m->sqrt(0x0000), 0x0000);
    ASSERT_UNSIGNED_EQ(m->sqrt(0x8000), 0x0000);
    ASSERT_UNSIGNED_EQ(m->sqrt(0x7F80), 0x7F80);
    ASSERT_UNSIGNED_EQ(m->sqrt(0xFF80), BF16_NAN);
    return TEST_PASS;
}

static const struct impl c_impl = {bf16_from_f32, bf16_mul, bf16_sqrt};
#ifdef __riscv
static const struct impl asm_impl = {bf16_from_f32_asm, bf16_mul_asm,
                                     bf16_sqrt_asm};
#endif

#define DUAL(name)                    \
    static int name##_c(void)         \
    {                                 \
        return check_##name(&c_impl); \
    }                                 \
    DUAL_ASM(name)

#ifdef __riscv
#define DUAL_ASM(name)                  \
    static int name##_asm(void)         \
    {                                   \
        return check_##name(&asm_impl); \
    }
#define REG(name)                                                 \
    do {                                                          \
        register_test(name##_c, NULL, NULL, "bf16_c_" #name);     \
        register_test(name##_asm, NULL, NULL, "bf16_asm_" #name); \
    } while (0)
#else
#define DUAL_ASM(name)
#define REG(name) register_test(name##_c, NULL, NULL, "bf16_c_" #name)
#endif

DUAL(f32_cases)
DUAL(f32_low_halves)
DUAL(f32_high_halves)
DUAL(mul_cases)
DUAL(mul_grid)
DUAL(mul_columns)
DUAL(mul_laws)
DUAL(sqrt_cases)
DUAL(sqrt_all)
DUAL(sqrt_laws)
DUAL(sqrt_classes)

#ifdef __riscv
/* The assembly must equal the C version where the oracle has no opinion. */
static int asm_matches_c(void)
{
    uint32_t x = 0x600DF00Du;
    for (int n = 0; n < 8192; n++) {
        x = xs32(x);
        uint16_t a = (uint16_t) (x >> 16), b = (uint16_t) x;
        ASSERT_UNSIGNED_EQ(bf16_mul_asm(a, b), bf16_mul(a, b));
        ASSERT_UNSIGNED_EQ(bf16_sqrt_asm(a), bf16_sqrt(a));
        ASSERT_UNSIGNED_EQ(bf16_from_f32_asm(x), bf16_from_f32(x));
    }
    return TEST_PASS;
}
#endif

void bf16_register_tests(void)
{
    REG(f32_cases);
    REG(f32_low_halves);
    REG(f32_high_halves);
    REG(mul_cases);
    REG(mul_grid);
    REG(mul_columns);
    REG(mul_laws);
    REG(sqrt_cases);
    REG(sqrt_all);
    REG(sqrt_laws);
    REG(sqrt_classes);
#ifdef __riscv
    register_test(asm_matches_c, NULL, NULL, "bf16_asm_matches_c");
#endif
}

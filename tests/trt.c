#include <stddef.h>
#include <stdint.h>
#include "print.h"
#include "rt.h"
#include "tests.h"
#include "tutil.h"
#include "utest.h"

extern uint64_t get_cycles(void);
extern uint64_t get_instret(void);

/* ---- __mulsi3 ---- */

/* Independent product: byte-wise partial products, each formed by repeated
 * addition, so it uses neither shifts of the multiplier nor __mulsi3. */
static uint32_t ref_mul(uint32_t a, uint32_t b)
{
    uint32_t r = 0;
    for (int byte = 0; byte < 4; byte++) {
        uint32_t digit = (b >> (8 * byte)) & 0xFF, part = 0;
        for (uint32_t k = 0; k < digit; k++)
            part += a;
        r += part << (8 * byte);
    }
    return r;
}

static int test_mulsi3_small(void)
{
    for (uint32_t a = 0; a < 128; a++) {
        for (uint32_t b = 0; b < 128; b++)
            ASSERT_UNSIGNED_EQ(__mulsi3(a, b), a * b == 0 ? 0 : ref_mul(a, b));
    }
    return TEST_PASS;
}

static int test_mulsi3_edges(void)
{
    ASSERT_UNSIGNED_EQ(__mulsi3(0, 0xFFFFFFFFu), 0);
    ASSERT_UNSIGNED_EQ(__mulsi3(0xFFFFFFFFu, 0), 0);
    ASSERT_UNSIGNED_EQ(__mulsi3(1, 0xFFFFFFFFu), 0xFFFFFFFFu);
    ASSERT_UNSIGNED_EQ(__mulsi3(0xFFFFFFFFu, 0xFFFFFFFFu), 1);
    ASSERT_UNSIGNED_EQ(__mulsi3(0x80000000u, 2), 0);
    ASSERT_UNSIGNED_EQ(__mulsi3(0x10000u, 0x10000u), 0);
    ASSERT_UNSIGNED_EQ(__mulsi3(0xFFFFu, 0xFFFFu), 0xFFFE0001u);
    ASSERT_UNSIGNED_EQ(__mulsi3(0x7FFFFFFFu, 2), 0xFFFFFFFEu);
    for (int i = 0; i < 32; i++)
        ASSERT_UNSIGNED_EQ(__mulsi3(1u << i, 3), 3u << i);
    return TEST_PASS;
}

static int test_mulsi3_random(void)
{
    uint32_t x = 0xC001CAFEu;
    for (int k = 0; k < 1024; k++) {
        x = xs32(x);
        uint32_t a = x;
        x = xs32(x);
        ASSERT_UNSIGNED_EQ(__mulsi3(a, x), ref_mul(a, x));
        ASSERT_UNSIGNED_EQ(__mulsi3(x, a), __mulsi3(a, x));
    }
    return TEST_PASS;
}

/* ---- memory and string helpers ---- */

static int test_memcpy_memset(void)
{
    uint8_t src[40], dst[48];
    for (int i = 0; i < 40; i++)
        src[i] = (uint8_t) (i * 7 + 1);
    for (size_t n = 0; n <= 40; n++) {
        for (int i = 0; i < 48; i++)
            dst[i] = 0xEE;
        ASSERT_PTR_EQ(memcpy(dst + 3, src, n), dst + 3);
        ASSERT_MEM_EQ(dst + 3, src, n);
        ASSERT_EQ(dst[2], 0xEE);
        ASSERT_EQ(dst[3 + n], 0xEE);
    }
    for (size_t n = 0; n <= 40; n++) {
        for (int i = 0; i < 48; i++)
            dst[i] = 0x11;
        ASSERT_PTR_EQ(memset(dst + 5, 0xAB, n), dst + 5);
        for (size_t k = 0; k < n; k++)
            ASSERT_EQ(dst[5 + k], 0xAB);
        ASSERT_EQ(dst[4], 0x11);
        ASSERT_EQ(dst[5 + n], 0x11);
    }
    volatile int wide = 0x1FF; /* only the low byte is stored */
    memset(dst, wide, 8);
    ASSERT_EQ(dst[0], 0xFF);
    return TEST_PASS;
}

/* Overlapping copies in both directions give the memmove result. */
static int test_memmove(void)
{
    uint8_t buf[32], want[32];
    for (int shift = -8; shift <= 8; shift++) {
        for (int i = 0; i < 32; i++)
            buf[i] = want[i] = (uint8_t) (i + 1);
        int from = 8, to = 8 + shift;
        uint8_t tmp[16];
        for (int i = 0; i < 16; i++)
            tmp[i] = want[from + i];
        for (int i = 0; i < 16; i++)
            want[to + i] = tmp[i];
        ASSERT_PTR_EQ(memmove(buf + to, buf + from, 16), buf + to);
        ASSERT_MEM_EQ(buf, want, 32);
    }
    ASSERT_PTR_EQ(memmove(buf, buf, 0), buf);
    return TEST_PASS;
}

static int test_memcmp_strlen(void)
{
    /* The strings go through volatile pointers so the checks run the code
     * under test instead of being folded away. */
    const char *volatile abc = "abc", *volatile abd = "abd";
    ASSERT_EQ(memcmp(abc, "abc", 3), 0);
    ASSERT_EQ(memcmp(abc, abd, 3) < 0, 1);
    ASSERT_EQ(memcmp(abd, abc, 3) > 0, 1);
    ASSERT_EQ(memcmp(abc, abd, 2), 0);
    ASSERT_EQ(memcmp(abc, abd, 0), 0);
    const char *volatile lo = "\x01", *volatile hi = "\xFF";
    ASSERT_EQ(memcmp(lo, hi, 1) < 0, 1); /* bytes compare unsigned */
    const char *volatile empty = "", *volatile one = "a";
    const char *volatile hello = "hello, world", *volatile nul = "ab\0cd";
    ASSERT_EQ(strlen(empty), 0);
    ASSERT_EQ(strlen(one), 1);
    ASSERT_EQ(strlen(hello), 12);
    ASSERT_EQ(strlen(nul), 2);
    return TEST_PASS;
}

/* ---- printer ---- */

static int test_printf_integers(void)
{
    char b[48];
    ASSERT_EQ(bm_snprintf(b, sizeof b, "%d", 0), 1);
    ASSERT_MEM_EQ(b, "0", 2);
    bm_snprintf(b, sizeof b, "%d|%d|%d", 7, -7, -2147483647 - 1);
    ASSERT_MEM_EQ(b, "7|-7|-2147483648", 17);
    bm_snprintf(b, sizeof b, "%u|%u", 4294967295u, 10u);
    ASSERT_MEM_EQ(b, "4294967295|10", 14);
    bm_snprintf(b, sizeof b, "%lld", -9223372036854775807ll - 1);
    ASSERT_MEM_EQ(b, "-9223372036854775808", 21);
    bm_snprintf(b, sizeof b, "%lld|%lld", 9223372036854775807ll, -1ll);
    ASSERT_MEM_EQ(b, "9223372036854775807|-1", 22);
    bm_snprintf(b, sizeof b, "%llu", 18446744073709551615ull);
    ASSERT_MEM_EQ(b, "18446744073709551615", 21);
    bm_snprintf(b, sizeof b, "%llu|%llu", 0ull, 4294967296ull);
    ASSERT_MEM_EQ(b, "0|4294967296", 13);
    for (unsigned i = 0; i < 20; i++) { /* every power of ten */
        unsigned long long p = 1;
        for (unsigned k = 0; k < i; k++)
            p = (p << 3) + (p << 1);
        char want[24];
        want[0] = '1';
        for (unsigned k = 0; k < i; k++)
            want[1 + k] = '0';
        want[1 + i] = '\0';
        bm_snprintf(b, sizeof b, "%llu", p);
        ASSERT_MEM_EQ(b, want, i + 2);
    }
    return TEST_PASS;
}

static int test_printf_other(void)
{
    char b[48];
    bm_snprintf(b, sizeof b, "%x|%x|%llx", 0u, 0xBEEFu, 0x123456789ABCDEFull);
    ASSERT_MEM_EQ(b, "0|beef|123456789abcdef", 23);
    bm_snprintf(b, sizeof b, "%s|%c|%%|%s", "str", 'z', "");
    ASSERT_MEM_EQ(b, "str|z|%|", 9);
    bm_snprintf(b, sizeof b, "%p", (void *) 0x10000);
    ASSERT_MEM_EQ(b, "0x10000", 8);
    bm_snprintf(b, sizeof b, "%p", (void *) 0);
    ASSERT_MEM_EQ(b, "0x0", 4);
    bm_snprintf(b, sizeof b, "plain text");
    ASSERT_MEM_EQ(b, "plain text", 11);
    return TEST_PASS;
}

/* Output is cut to cap - 1 characters, terminated, and the full length is
 * returned; cap 0 writes nothing. */
static int test_printf_truncation(void)
{
    char b[8];
    for (int i = 0; i < 8; i++)
        b[i] = '#';
    ASSERT_EQ(bm_snprintf(b, 4, "%s", "abcdef"), 6);
    ASSERT_MEM_EQ(b, "abc", 4);
    ASSERT_EQ(b[4], '#');
    ASSERT_EQ(bm_snprintf(b, 7, "%s", "abcdef"), 6);
    ASSERT_MEM_EQ(b, "abcdef", 7);
    ASSERT_EQ(bm_snprintf(b, 1, "%s", "abc"), 3);
    ASSERT_EQ(b[0], '\0');
    b[0] = '#';
    ASSERT_EQ(bm_snprintf(b, 0, "%s", "abc"), 3);
    ASSERT_EQ(b[0], '#');
    return TEST_PASS;
}

/* ---- startup and counters ---- */

static volatile uint32_t zeroed[64];
static volatile uint32_t initialized[4] = {1, 2, 3, 4};

static int test_startup(void)
{
    for (int i = 0; i < 64; i++)
        ASSERT_UNSIGNED_EQ(zeroed[i], 0); /* .bss was cleared by start.S */
    for (uint32_t i = 0; i < 4; i++)
        ASSERT_UNSIGNED_EQ(initialized[i], i + 1); /* .data is loaded */
    uintptr_t sp;
    __asm__ volatile("mv %0, sp" : "=r"(sp));
    ASSERT_UNSIGNED_EQ(sp & 15u, 0);
    return TEST_PASS;
}

static int test_counters(void)
{
    uint64_t c0 = get_cycles(), i0 = get_instret();
    volatile uint32_t spin = 0;
    for (int k = 0; k < 1000; k++)
        spin += (uint32_t) k;
    uint64_t c1 = get_cycles(), i1 = get_instret();
    ASSERT_TRUE(c1 > c0);
    ASSERT_TRUE(i1 > i0);
    ASSERT_TRUE(c1 - c0 >= 1000); /* at least one instruction per iteration */
    ASSERT_TRUE(i1 - i0 >= 1000);
    uint64_t c2 = get_cycles();
    ASSERT_TRUE(c2 >= c1);
    return TEST_PASS;
}

void rt_register_tests(void)
{
    register_test(test_mulsi3_small, NULL, NULL, "rt_mulsi3_small");
    register_test(test_mulsi3_edges, NULL, NULL, "rt_mulsi3_edges");
    register_test(test_mulsi3_random, NULL, NULL, "rt_mulsi3_random");
    register_test(test_memcpy_memset, NULL, NULL, "rt_memcpy_memset");
    register_test(test_memmove, NULL, NULL, "rt_memmove");
    register_test(test_memcmp_strlen, NULL, NULL, "rt_memcmp_strlen");
    register_test(test_printf_integers, NULL, NULL, "rt_printf_integers");
    register_test(test_printf_other, NULL, NULL, "rt_printf_other");
    register_test(test_printf_truncation, NULL, NULL, "rt_printf_truncation");
    register_test(test_startup, NULL, NULL, "rt_startup");
    register_test(test_counters, NULL, NULL, "rt_counters");
}

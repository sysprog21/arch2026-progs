#include <stddef.h>
#include "utest.h"

/* Checks of the assertion macros themselves: each failing assertion lives in
 * its own function so the test can observe TEST_FAIL instead of failing. The
 * failing helpers print their message, which is expected output here.
 */
static int quiet(const char *fmt, ...)
{
    (void) fmt;
    return 0;
}

#define EXPECT_FAIL(call)                                \
    do {                                                 \
        int (*saved)(const char *, ...) = utest_printer; \
        utest_printer = quiet;                           \
        int r_ = (call);                                 \
        utest_printer = saved;                           \
        if (r_ != TEST_FAIL)                             \
            UTEST_FAILED(#call " did not fail\n");       \
    } while (0)

#define EXPECT_PASS(call)                    \
    do {                                     \
        if ((call) != TEST_PASS)             \
            UTEST_FAILED(#call " failed\n"); \
    } while (0)

static int f_true(int v)
{
    ASSERT_TRUE(v);
    return TEST_PASS;
}
static int f_false(int v)
{
    ASSERT_FALSE(v);
    return TEST_PASS;
}
static int f_eq(long long a, long long b)
{
    ASSERT_EQ(a, b);
    return TEST_PASS;
}
static int f_neq(long long a, long long b)
{
    ASSERT_NEQ(a, b);
    return TEST_PASS;
}
static int f_ueq(unsigned long long a, unsigned long long b)
{
    ASSERT_UNSIGNED_EQ(a, b);
    return TEST_PASS;
}
static int f_uneq(unsigned long long a, unsigned long long b)
{
    ASSERT_UNSIGNED_NEQ(a, b);
    return TEST_PASS;
}
static int f_aeq(long long a, long long b, long long d)
{
    ASSERT_ALMOST_EQ(a, b, d);
    return TEST_PASS;
}
static int f_uaeq(unsigned long long a,
                  unsigned long long b,
                  unsigned long long d)
{
    ASSERT_UNSIGNED_ALMOST_EQ(a, b, d);
    return TEST_PASS;
}
static int f_peq(const void *a, const void *b)
{
    ASSERT_PTR_EQ(a, b);
    return TEST_PASS;
}
static int f_pneq(const void *a, const void *b)
{
    ASSERT_PTR_NEQ(a, b);
    return TEST_PASS;
}
static int f_null(const void *a)
{
    ASSERT_NULL(a);
    return TEST_PASS;
}
static int f_nnull(const void *a)
{
    ASSERT_NOT_NULL(a);
    return TEST_PASS;
}
static int f_mem(const void *a, const void *b, unsigned n)
{
    ASSERT_MEM_EQ(a, b, n);
    return TEST_PASS;
}
static int f_fail(void)
{
    FAIL();
    return TEST_PASS;
}

static int test_boolean(void)
{
    EXPECT_PASS(f_true(1));
    EXPECT_PASS(f_true(-5));
    EXPECT_FAIL(f_true(0));
    EXPECT_PASS(f_false(0));
    EXPECT_FAIL(f_false(1));
    EXPECT_FAIL(f_fail());
    return TEST_PASS;
}

/* Signed and unsigned equality differ for values with the top bit set. */
static int test_equality(void)
{
    EXPECT_PASS(f_eq(-1, -1));
    EXPECT_FAIL(f_eq(-1, 1));
    EXPECT_PASS(f_neq(-1, 1));
    EXPECT_FAIL(f_neq(7, 7));
    EXPECT_PASS(f_ueq(0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull));
    EXPECT_FAIL(f_ueq(0x8000000000000000ull, 0x7FFFFFFFFFFFFFFFull));
    EXPECT_PASS(f_uneq(0, 1));
    EXPECT_FAIL(f_uneq(9, 9));
    ASSERT_EQ((int) 0xFFFFFFFFu, -1);
    ASSERT_UNSIGNED_EQ((unsigned) -1, 0xFFFFFFFFu);
    return TEST_PASS;
}

/* The distance is symmetric and never wraps, including for the extremes that
 * break an ABS-style macro.
 */
static int test_almost_equal(void)
{
    EXPECT_PASS(f_aeq(10, 12, 2));
    EXPECT_PASS(f_aeq(12, 10, 2));
    EXPECT_FAIL(f_aeq(10, 13, 2));
    EXPECT_FAIL(f_aeq(13, 10, 2));
    EXPECT_PASS(f_aeq(-5, 5, 10));
    EXPECT_FAIL(f_aeq(-5, 5, 9));
    EXPECT_FAIL(f_aeq(-0x7FFFFFFFFFFFFFFFll - 1, 0x7FFFFFFFFFFFFFFFll, 1));
    EXPECT_PASS(f_uaeq(3, 5, 2));
    EXPECT_PASS(f_uaeq(5, 3, 2));
    EXPECT_FAIL(f_uaeq(3, 6, 2));
    EXPECT_FAIL(f_uaeq(6, 3, 2));
    EXPECT_FAIL(f_uaeq(0, 0xFFFFFFFFFFFFFFFFull, 5));
    EXPECT_PASS(f_uaeq(0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFEull, 1));
    return TEST_PASS;
}

static int test_pointers_and_memory(void)
{
    int x = 0, y = 0;
    EXPECT_PASS(f_peq(&x, &x));
    EXPECT_FAIL(f_peq(&x, &y));
    EXPECT_PASS(f_pneq(&x, &y));
    EXPECT_FAIL(f_pneq(&x, &x));
    EXPECT_PASS(f_null(NULL));
    EXPECT_FAIL(f_null(&x));
    EXPECT_PASS(f_nnull(&x));
    EXPECT_FAIL(f_nnull(NULL));
    EXPECT_PASS(f_mem("abcd", "abcd", 4));
    EXPECT_PASS(f_mem("abcd", "abxx", 2));
    EXPECT_FAIL(f_mem("abcd", "abxx", 3));
    EXPECT_PASS(f_mem("a", "b", 0));
    return TEST_PASS;
}

static int ran_setup, ran_teardown;
static void mark_setup(void)
{
    ran_setup = 1;
}
static void mark_teardown(void)
{
    ran_teardown = 1;
}

static int test_hooks_body(void)
{
    ASSERT_EQ(ran_setup, 1);
    return TEST_PASS;
}

static int test_hooks_after(void)
{
    ASSERT_EQ(ran_teardown, 1);
    return TEST_PASS;
}

void utest_register_tests(void)
{
    register_test(test_boolean, NULL, NULL, "utest_boolean");
    register_test(test_equality, NULL, NULL, "utest_equality");
    register_test(test_almost_equal, NULL, NULL, "utest_almost_equal");
    register_test(test_pointers_and_memory, NULL, NULL,
                  "utest_pointers_memory");
    register_test(test_hooks_body, mark_setup, mark_teardown, "utest_setup");
    register_test(test_hooks_after, NULL, NULL, "utest_teardown");
}

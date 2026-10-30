#pragma once

/* micro-test: a small unit-test framework for hosted and bare-metal C.
 *
 * It has no libc dependency. A test is an int function that returns TEST_PASS
 * or, through an assertion, TEST_FAIL. Register tests, then run them:
 *
 *   static int test_add(void) { ASSERT_EQ(1 + 1, 2); return TEST_PASS; }
 *   register_test(test_add, NULL, NULL, "add");
 *   return run_tests();             // 0 when every test passed, 1 otherwise
 *
 * Output goes through utest_printer, a printf-like function pointer. The
 * default is printf on a hosted build and bm_printf (common/print.c) on RISC-V;
 * assign another one before the first register_test() to redirect it.
 *
 * Limits: UTEST_MAX_TESTS registrations (further ones are rejected with a
 * message), names of at most UTEST_NAME_SIZE - 1 characters (longer ones are
 * cut and stay NUL-terminated). Assertions print through %lld, %llu and %p, so
 * a custom printer must support those conversions. There are no floating-point
 * assertions: compare the bit patterns, or state a tolerance on integers.
 */

#define TEST_PASS 0
#define TEST_FAIL 1

#define UTEST_MAX_TESTS 256
#define UTEST_NAME_SIZE 32

typedef long long int_t;
typedef unsigned long long uint_t;

typedef int (*int_fn_void)(void);
typedef void (*void_fn_void)(void);

extern int (*utest_printer)(const char *, ...);

/* setup and teardown may be NULL. */
void register_test(int_fn_void fn,
                   void_fn_void setup,
                   void_fn_void teardown,
                   const char *name);

/* Run every registered test and print a summary. Returns 0 if all passed. */
int run_tests(void);

/* Number of tests registered so far. */
int utest_count(void);

#define UTEST_FAILED(...)                          \
    do {                                           \
        utest_printer(__FILE__ "+%d: ", __LINE__); \
        utest_printer(__VA_ARGS__);                \
        return TEST_FAIL;                          \
    } while (0)

#define FAIL() UTEST_FAILED("explicit fail\n")

#define ASSERT_TRUE(A)                               \
    do {                                             \
        if (!(A))                                    \
            UTEST_FAILED("Expected true: " #A "\n"); \
    } while (0)

#define ASSERT_FALSE(A)                               \
    do {                                              \
        if (A)                                        \
            UTEST_FAILED("Expected false: " #A "\n"); \
    } while (0)

/* Signed comparison: both sides are widened to long long. */
#define ASSERT_EQ(A, B)                                               \
    do {                                                              \
        int_t a_ = (int_t) (A), b_ = (int_t) (B);                     \
        if (a_ != b_)                                                 \
            UTEST_FAILED("Expected = %lld, Actual = %lld\n", a_, b_); \
    } while (0)

#define ASSERT_NEQ(A, B)                                               \
    do {                                                               \
        int_t a_ = (int_t) (A), b_ = (int_t) (B);                      \
        if (a_ == b_)                                                  \
            UTEST_FAILED("Expected != %lld, Actual = %lld\n", a_, b_); \
    } while (0)

/* Unsigned comparison: both sides are widened to unsigned long long. */
#define ASSERT_UNSIGNED_EQ(A, B)                                      \
    do {                                                              \
        uint_t a_ = (uint_t) (A), b_ = (uint_t) (B);                  \
        if (a_ != b_)                                                 \
            UTEST_FAILED("Expected = %llu, Actual = %llu\n", a_, b_); \
    } while (0)

#define ASSERT_UNSIGNED_NEQ(A, B)                                      \
    do {                                                               \
        uint_t a_ = (uint_t) (A), b_ = (uint_t) (B);                   \
        if (a_ == b_)                                                  \
            UTEST_FAILED("Expected != %llu, Actual = %llu\n", a_, b_); \
    } while (0)

/* |A - B| <= D. The distance is formed without overflow or wrap-around. */
#define ASSERT_ALMOST_EQ(A, B, D)                                             \
    do {                                                                      \
        int_t a_ = (int_t) (A), b_ = (int_t) (B), d_ = (int_t) (D);           \
        uint_t dist_ =                                                        \
            a_ >= b_ ? (uint_t) a_ - (uint_t) b_ : (uint_t) b_ - (uint_t) a_; \
        if (dist_ > (uint_t) d_)                                              \
            UTEST_FAILED("Expected %lld within %lld of %lld\n", a_, d_, b_);  \
    } while (0)

#define ASSERT_UNSIGNED_ALMOST_EQ(A, B, D)                                   \
    do {                                                                     \
        uint_t a_ = (uint_t) (A), b_ = (uint_t) (B), d_ = (uint_t) (D);      \
        uint_t dist_ = a_ >= b_ ? a_ - b_ : b_ - a_;                         \
        if (dist_ > d_)                                                      \
            UTEST_FAILED("Expected %llu within %llu of %llu\n", a_, d_, b_); \
    } while (0)

#define ASSERT_PTR_EQ(A, B)                                       \
    do {                                                          \
        const void *a_ = (A), *b_ = (B);                          \
        if (a_ != b_)                                             \
            UTEST_FAILED("Expected = %p, Actual = %p\n", a_, b_); \
    } while (0)

#define ASSERT_PTR_NEQ(A, B)                                       \
    do {                                                           \
        const void *a_ = (A), *b_ = (B);                           \
        if (a_ == b_)                                              \
            UTEST_FAILED("Expected != %p, Actual = %p\n", a_, b_); \
    } while (0)

#define ASSERT_NULL(A)                                        \
    do {                                                      \
        const void *a_ = (A);                                 \
        if (a_)                                               \
            UTEST_FAILED("Expected NULL, Actual = %p\n", a_); \
    } while (0)

#define ASSERT_NOT_NULL(A)                             \
    do {                                               \
        const void *a_ = (A);                          \
        if (!a_)                                       \
            UTEST_FAILED("Unexpected NULL: " #A "\n"); \
    } while (0)

/* N bytes of A and B must match. Reports the first differing offset. */
#define ASSERT_MEM_EQ(A, B, N)                                       \
    do {                                                             \
        const unsigned char *a_ = (const unsigned char *) (A);       \
        const unsigned char *b_ = (const unsigned char *) (B);       \
        for (unsigned long i_ = 0; i_ < (unsigned long) (N); i_++) { \
            if (a_[i_] != b_[i_])                                    \
                UTEST_FAILED("Byte %lu differs: %llu vs %llu\n", i_, \
                             (uint_t) a_[i_], (uint_t) b_[i_]);      \
        }                                                            \
    } while (0)

#include "utest.h"

/* Hosted builds print with printf; RISC-V bare-metal builds with bm_printf. */
#ifdef __riscv
#include "print.h"
#define UTEST_DEFAULT_PRINTER bm_printf
#else
#include <stdio.h>
#define UTEST_DEFAULT_PRINTER printf
#endif

int (*utest_printer)(const char *, ...) = UTEST_DEFAULT_PRINTER;

struct test {
    int_fn_void fn;
    void_fn_void setup;
    void_fn_void teardown;
    char name[UTEST_NAME_SIZE];
};

static struct test tests[UTEST_MAX_TESTS];
static int ntests;

int utest_count(void)
{
    return ntests;
}

void register_test(int_fn_void fn,
                   void_fn_void setup,
                   void_fn_void teardown,
                   const char *name)
{
    if (ntests >= UTEST_MAX_TESTS) {
        utest_printer("Too many tests (max tests = %d)!\n", UTEST_MAX_TESTS);
        return;
    }
    struct test *t = &tests[ntests++];
    t->fn = fn;
    t->setup = setup;
    t->teardown = teardown;
    /* Copy at most UTEST_NAME_SIZE - 1 characters; always terminate. */
    int i = 0;
    for (; i < UTEST_NAME_SIZE - 1 && name[i]; i++)
        t->name[i] = name[i];
    t->name[i] = '\0';
}

int run_tests(void)
{
    int passed = 0, failed = 0;
    for (int i = 0; i < ntests; i++) {
        struct test *t = &tests[i];
        utest_printer("%s ... ", t->name);
        if (t->setup)
            t->setup();
        int r = t->fn();
        if (t->teardown)
            t->teardown();
        if (r == TEST_PASS) {
            utest_printer("ok\n");
            passed++;
        } else {
            utest_printer("failed\n");
            failed++;
        }
    }
    utest_printer("Summary: %d passed, %d failed.\n", passed, failed);
    return failed != 0;
}

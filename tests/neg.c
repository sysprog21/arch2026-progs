#include <stddef.h>
#include "print.h"
#include "utest.h"

/* Negative control: one passing and one deliberately failing test. The
 * failure must show in the output and in the value main returns. */
static int test_passes(void)
{
    ASSERT_EQ(1 + 1, 2);
    return TEST_PASS;
}

static int test_fails(void)
{
    ASSERT_EQ(1 + 1, 3);
    return TEST_PASS;
}

int main(void)
{
    register_test(test_passes, NULL, NULL, "neg_passes");
    register_test(test_fails, NULL, NULL, "neg_fails");
    return run_tests();
}

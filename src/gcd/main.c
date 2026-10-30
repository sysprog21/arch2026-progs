#include "bench.h"
#include "gcd.h"
#include "print.h"
#include "tests.h"
#include "utest.h"

/* Fixed inputs for the timed loops: identical for the C and assembly runs. */
static const uint32_t inputs[] = {
    1u,     2u,          6u,          48u,        1071u,      462u,
    65536u, 0xffffffffu, 0x80000000u, 123456789u, 987654321u, 0x7fffffffu,
    270u,   192u,        17u,         0u,
};
#define N (sizeof(inputs) / sizeof(inputs[0]))

int main(void)
{
    rt_register_tests();
    utest_register_tests();
    gcd_register_tests();
    int rc = run_tests();
    bm_printf("@@prog name=gcd opt=%s tests_rc=%d\n", OPT_NAME, rc);
    BENCH("gcd", "gcd", "c", inputs, N, gcd_u32(in, (in >> 7) | 1u));
    BENCH("gcd", "gcd", "asm", inputs, N, gcd_u32_asm(in, (in >> 7) | 1u));
    return rc;
}

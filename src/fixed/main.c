#include "bench.h"
#include "fixed.h"
#include "print.h"
#include "tests.h"
#include "utest.h"

/* Fixed inputs for the timed loops: identical for the C and assembly runs. */
static const uint32_t inputs[] = {
    0u,          0x00100000u, 0xfff00000u, 0x00180000u,
    0x7fffffffu, 0x80000000u, 0x000fffffu, 0xfff00001u,
    0x00080000u, 0xfff80000u, 0x00000001u, 0xffffffffu,
    0x01234567u, 0xfedcba98u, 0x40000000u, 0xc0000000u,
};
#define N (sizeof(inputs) / sizeof(inputs[0]))

/* op1 also reports its pass count; fold it into the result. */
static uint32_t run_op1(uint32_t a, uint32_t b)
{
    int n;
    return op1(a, b, &n) + (uint32_t) n;
}

static uint32_t run_op1_asm(uint32_t a, uint32_t b)
{
    int n;
    return op1_asm(a, b, &n) + (uint32_t) n;
}

int main(void)
{
    rt_register_tests();
    utest_register_tests();
    fixed_register_tests();
    int rc = run_tests();
    bm_printf("@@prog name=fixed opt=%s tests_rc=%d\n", OPT_NAME, rc);
    BENCH("fixed", "op1", "c", inputs, N, run_op1(in, in >> 3));
    BENCH("fixed", "op1", "asm", inputs, N, run_op1_asm(in, in >> 3));
    BENCH("fixed", "op2", "c", inputs, N, op2(in, in >> 3));
    BENCH("fixed", "op2", "asm", inputs, N, op2_asm(in, in >> 3));
    BENCH("fixed", "op3", "c", inputs, N, op3(in, in >> 3));
    BENCH("fixed", "op3", "asm", inputs, N, op3_asm(in, in >> 3));
    BENCH("fixed", "q_mul", "c", inputs, N,
          q_mul((int32_t) in, (int32_t) (in >> 5)));
    BENCH("fixed", "q_mul", "asm", inputs, N,
          q_mul_asm((int32_t) in, (int32_t) (in >> 5)));
    return rc;
}

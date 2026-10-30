#include "bench.h"
#include "bf16.h"
#include "print.h"
#include "tests.h"
#include "utest.h"

/* Fixed inputs for the timed loops: two bf16 patterns per word. */
static const uint32_t inputs[] = {
    0x3f803f80u, 0x40004000u, 0x3fc04040u, 0x7f807f80u,
    0x00800080u, 0xbf80bf80u, 0x3f000000u, 0x42280000u,
    0x3dcd3dcdu, 0x41204120u, 0x7f7f7f7fu, 0x00010001u,
    0x80008000u, 0x7fc07fc0u, 0x40490fdbu, 0x3f9a3f9au,
};
#define N (sizeof(inputs) / sizeof(inputs[0]))

int main(void)
{
    rt_register_tests();
    utest_register_tests();
    bf16_register_tests();
    int rc = run_tests();
    bm_printf("@@prog name=bf16 opt=%s tests_rc=%d\n", OPT_NAME, rc);
    BENCH("bf16", "from_f32", "c", inputs, N, bf16_from_f32(in));
    BENCH("bf16", "from_f32", "asm", inputs, N, bf16_from_f32_asm(in));
    BENCH("bf16", "mul", "c", inputs, N, bf16_mul(in >> 16, in & 0xffff));
    BENCH("bf16", "mul", "asm", inputs, N, bf16_mul_asm(in >> 16, in & 0xffff));
    BENCH("bf16", "sqrt", "c", inputs, N, bf16_sqrt(in & 0xffff));
    BENCH("bf16", "sqrt", "asm", inputs, N, bf16_sqrt_asm(in & 0xffff));
    return rc;
}

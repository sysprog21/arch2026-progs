#include "bench.h"
#include "print.h"
#include "src/rubik/rubik.h"
#include "tests.h"
#include "utest.h"

/* States of distance 0, 2, 4, 6, 8 and 9 for the timed loops. */
static const char *const states[] = {
    "12345671111111", "46253172123131", "43562713121333",
    "72465131321333", "62345713133111", "24513763133333",
};
static const uint32_t inputs[] = {0, 1, 2, 3, 4, 5};
#define N (sizeof(inputs) / sizeof(inputs[0]))

static char buf[64];

int main(void)
{
    rt_register_tests();
    utest_register_tests();
    rubik_register_tests();
    int rc = run_tests();
    bm_printf("@@prog name=rubik opt=%s tests_rc=%d\n", OPT_NAME, rc);
    BENCH("rubik", "solve", "c", inputs, N,
          rubik_solve(states[in], buf, sizeof buf));
    BENCH("rubik", "solve", "asm", inputs, N,
          rubik_solve_asm(states[in], buf, sizeof buf));
    return rc;
}

#include "tests.h"
#include "utest.h"

/* Host runner: registers the tests of one program and runs them under the
 * sanitizers. The exit status is 0 whenever the tests ran; failures are
 * judged on the target by `make test`. */
extern void PROG_REGISTER(void);

int main(void)
{
    utest_register_tests();
    PROG_REGISTER();
    (void) run_tests();
    return 0;
}

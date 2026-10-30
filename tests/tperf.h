#pragma once

#include <stdint.h>

/* Instruction budgets. rv32emu counts executed guest instructions in the
 * instret counter, so a budget is exact and repeatable: the same ELF gives the
 * same count on every run and on every host. The helper exists only on the
 * target; the host runs of the tests (sanitizers) skip every budget.
 *
 *   uint64_t used;
 *   PERF_CALL(used, result = f(x));   used = instructions f retired
 *
 * The cost of the two counter reads is subtracted.
 */
#ifdef __riscv
extern uint64_t get_instret(void);

/* Cost of two back-to-back counter reads. */
static inline uint64_t perf_overhead(void)
{
    uint64_t a = get_instret();
    uint64_t b = get_instret();
    return b - a;
}

#define PERF_CALL(used, call)                               \
    do {                                                    \
        uint64_t perf_t0_ = get_instret();                  \
        call;                                               \
        uint64_t perf_n_ = get_instret() - perf_t0_;        \
        uint64_t perf_o_ = perf_overhead();                 \
        (used) = perf_n_ > perf_o_ ? perf_n_ - perf_o_ : 0; \
    } while (0)
#endif

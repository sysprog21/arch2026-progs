#pragma once

#include <stdint.h>
#include "print.h"

/*
 * Cycle accounting for rv32emu. The emulator has no timing model, so cycle
 * and instret are one counter that counts executed guest instructions.
 *
 * Every benchmark line has a fixed format that scripts/cycles.py parses:
 *
 *   @@bench prog=<p> fn=<f> impl=<c|asm> opt=<O1|O2|O3|Os> n=<calls>
 *           cycles=<total> instret=<total> overhead=<loop cycles>
 *           overhead_instret=<loop instret> checksum=<sum of results>
 *
 * cycles and instret cover the timed loop of n calls. overhead is the same
 * loop with the call replaced by a copy of its input: counter reads, loop
 * control, checksum and input load. The analysis subtracts it, so the
 * reported per-call cost is that of the measured function alone.
 */
uint64_t get_cycles(void);
uint64_t get_instret(void);

#ifndef OPT_NAME
#define OPT_NAME "unknown"
#endif

/* Keep a value alive so the optimizer cannot delete the measured work. */
#define BENCH_KEEP(x) __asm__ volatile("" : "+r"(x))

struct bench_sample {
    uint64_t cycles, instret;
};

static inline struct bench_sample bench_now(void)
{
    struct bench_sample s = {get_cycles(), get_instret()};
    return s;
}

static inline void bench_report(const char *prog,
                                const char *fn,
                                const char *impl,
                                unsigned n,
                                struct bench_sample total,
                                struct bench_sample overhead,
                                uint32_t checksum)
{
    bm_printf(
        "@@bench prog=%s fn=%s impl=%s opt=%s n=%u cycles=%llu "
        "instret=%llu overhead=%llu "
        "overhead_instret=%llu checksum=%u\n",
        prog, fn, impl, OPT_NAME, n, total.cycles, total.instret,
        overhead.cycles, overhead.instret, checksum);
}

/*
 * BENCH(prog, fn, impl, inputs, n, call): run `call` once per element of
 * `inputs` (a uint32_t array of n values; the element is named `in` inside
 * `call`), fold each result into a checksum, and report. A call that takes two
 * operands derives the second from `in` itself.
 */
#define BENCH(prog, fn, impl, inputs, n, call)                        \
    do {                                                              \
        uint32_t sink = 0, base = 0;                                  \
        struct bench_sample b0 = bench_now();                         \
        for (unsigned i_ = 0; i_ < (n); i_++) {                       \
            uint32_t in = (inputs)[i_];                               \
            uint32_t r_ = in;                                         \
            BENCH_KEEP(r_);                                           \
            base += r_;                                               \
        }                                                             \
        struct bench_sample b1 = bench_now();                         \
        struct bench_sample t0 = bench_now();                         \
        for (unsigned i_ = 0; i_ < (n); i_++) {                       \
            uint32_t in = (inputs)[i_];                               \
            uint32_t r_ = (uint32_t) (call);                          \
            BENCH_KEEP(r_);                                           \
            sink += r_;                                               \
        }                                                             \
        struct bench_sample t1 = bench_now();                         \
        BENCH_KEEP(sink);                                             \
        BENCH_KEEP(base);                                             \
        bench_report(prog, fn, impl, (n),                             \
                     (struct bench_sample) {t1.cycles - t0.cycles,    \
                                            t1.instret - t0.instret}, \
                     (struct bench_sample) {b1.cycles - b0.cycles,    \
                                            b1.instret - b0.instret}, \
                     sink);                                           \
    } while (0)

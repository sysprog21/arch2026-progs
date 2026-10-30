# arch2026-progs

Grading framework for [Assignment 2: Complete Applications](https://hackmd.io/@sysprog/2026-arch-homework2).
Four bare-metal RV32I programs run on [rv32emu](https://github.com/sysprog21/rv32emu) in system emulation, each
with a C version, a hand-written assembly version and a micro-test suite. The repository holds the Makefile, the
runtime, the test driver, the cycle analysis and the Git hooks. The programs themselves are stubs to be filled in.

| Program | Source | Assignment item |
|---|---|---|
| `rubik` | Homework 1, ported | 2 |
| `bf16` | Quiz 1 Problem B, BFloat16 | 3 |
| `gcd` | Quiz 2 Problem A, binary GCD | 3 |
| `fixed` | Quiz 3 Problem C, bitwise adders and Q11.20 fixed-point multiply | 3 |

## Prerequisites

* A RISC-V GNU toolchain with `riscv-none-elf-`, `riscv32-unknown-elf-` or `riscv64-unknown-elf-` as the prefix
  (for example [xPack](https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack)), GCC 12 or later.
  Set `CROSS_COMPILE=<prefix>` to choose one explicitly.
* The tools rv32emu needs for system emulation: a host C compiler, `make`, `git`, `curl`, and the device tree
  compiler (`dtc`). SDL2 is optional.
* Python 3.9 or later. `matplotlib` is needed only for the charts.
* For the Git hooks: `clang-format-20`, `cppcheck`, `aspell` with the English dictionary.

## Quick start

```shell
make              # install hooks, validate the toolchain, fetch and build the tools, build every ELF
make test         # grade every assignment item
make cycles       # measure the cycle counts, write tables and charts under results/
```

`make` clones the latest [rv32emu](https://github.com/sysprog21/rv32emu) into `third_party/rv32emu` (a shallow
clone of the default branch) and builds it with `system_interpreter_defconfig` plus `ENABLE_ELF_LOADER=1`. It is a
plain clone, not a Git submodule, and `third_party/` is never committed. `make update` fetches the newest commit and
rebuilds; `RV32EMU_REV=<commit-or-tag>` pins a revision instead. `make cycles` records the revision in
`results/cycles.md`, so a measurement stays interpretable when upstream moves.

| Target | What it does |
|---|---|
| `make toolchain` | Validate the toolchain: the compiler accepts `-march=rv32i_zicsr`, `*` lowers to `__mulsi3`, the ISA string has no M, F or C |
| `make deps` | Clone and build rv32emu if it is missing |
| `make update` | Fetch the latest rv32emu and rebuild it |
| `make elfs` | Build `build/<program>/<opt>/<program>.elf` for `-O1`, `-O2`, `-O3` and `-Os` |
| `make neg` | Build the negative control, whose failing test must make `main` return nonzero |
| `make test` | Run `scripts/driver.py`, which grades every item below |
| `make sanitize` | Build the C versions and tests for the host and run them under AddressSanitizer and UBSan |
| `make dump` | Write disassembly listings, `build/<program>/<opt>/<program>.dis` and `build/<program>/asm.dis` |
| `make size` | `size -A` of every implementation, harness excluded |
| `make cycles` | Cycle tables and charts under `results/` |
| `make indent` | Format with `clang-format-20` |

`-Ofast` is not used anywhere. `VERBOSE=1` shows the full commands.

## What `make test` checks

| Check | Assignment item | Points |
|---|---|---|
| `toolchain` | RV32I-only build flags | 5 |
| `emulator` | 1: rv32emu with system emulation and `ELF_LOADER`; `main`'s return value reaches the exit status | 10 |
| `rubik`, `bf16`, `gcd`, `fixed` | 2 and 3: per optimization level, only RV32I and the Zicsr counter reads, no undefined symbols, every micro-test passes, `main` returns 0, at least twenty tests, C and assembly agree on the benchmark inputs | 15 each |
| `disassembly` | 4: listings exist for every C build and for the assembly | 5 |
| `cycles` | 5: each function has cycle data for the four C builds and the assembly | 10 |
| `footprint` | 5: `size -A` works on every implementation | 5 |
| `hygiene` | no unsafe functions, no `-Ofast`, no non-RV32I `-march` | 5 |

`make test` also checks that the Git hooks are installed and that every commit follows the commit rules. Quality
of the HackMD note, the optimization results and the CodeTrial mock interview are reviewed by hand and are not
part of the script. Run `scripts/driver.py --help` for the options, for example `--prog gcd` and `--min-tests`.

The skeleton starts red on purpose: every program has a placeholder test that fails until it is replaced.

## Layout

```
Makefile, mk/        build rules; toolchain detection; rv32emu fetch and build
common/              start.S, linker.ld, perf.S, runtime helpers (rt), printer (print),
                     micro-test (utest.h, utest.c), benchmark macros (bench.h)
src/<program>/       <program>.h (the contract), <program>.c (dummy), <program>.S (stub), main.c
tests/               t<program>.c, <program>vec.h (golden vectors), trt.c (runtime, printer,
                     startup), tutest.c (micro-test), tutil.h, host.c, neg.c
scripts/             driver.py, cycles.py, rvcheck.py, toolchain.sh, benchlib.py, Git hooks
results/             generated cycle tables and charts
tools/oracle/        instructor-only reference models that generate tests/*vec.h (ignored by Git)
```

Each ELF links both implementations. The C version has the function's own name and the assembly version carries
an `_asm` suffix, so one test binary cross-checks them. `main.c` registers the tests, runs them, then prints the
benchmark lines; its return value is the micro-test result.

## Tests

`common/utest.h` and `common/utest.c` are a rewrite of [micro-test](https://github.com/trmckay/micro-test): the
same `register_test()`, `run_tests()`, `utest_printer` and `ASSERT_*` interface, no libc, one source for the host
and for bare metal. Differences from upstream: no output capture, so the code is not tied to `open`, `dup` and
`read`; names are cut at 31 characters and stay terminated; `ASSERT_ALMOST_EQ` and `ASSERT_UNSIGNED_ALMOST_EQ`
compute the distance without the upstream `ABS` macro bug; `ASSERT_MEM_EQ` is new; there are no floating-point
assertions. At most 256 tests can be registered, so a sweep belongs in one test function. Assertions print
through `%lld`, `%llu` and `%p`, which `common/print.c` formats by repeated subtraction, so it needs neither libgcc
nor a multiply.

## Test suite

The programs in `src/` are dummies on purpose: each function returns a constant, so every test of the assignment
fails until it is implemented. The suite is complete when a correct implementation passes it and a wrong one does
not. It was validated both ways: reference implementations (kept out of this repository) pass every test on the
host and on rv32emu at all four optimization levels, and mutants (wrong rounding, off-by-one boundaries, wrong
sign handling, an exact-fit buffer check) are rejected; the mutants that survived compute the same function as the
original.

`src/<program>/<program>.h` states the contract of each function in full: special values, rounding, wrap-around,
error returns. The tests enforce exactly that text.

| Program | Tests (each for C and assembly) | What they establish |
|---|---|---|
| `gcd` | directed cases, identities, all 65,536 pairs below 256, powers of two, shared factors of two, random operands, Fibonacci | against Euclid built on a restoring divider, plus gcd(F(n), F(m)) = F(gcd(n, m)) |
| `bf16` | narrowing: directed, every discarded half under 16 kept halves, every kept half under the five tie-deciding halves; multiply: directed, a 256 x 256 grid, 8 constants over all 65,536 operands, laws; square root: directed, all 65,536 inputs, monotonicity and a 1% bound, special classes | narrowing against an independent formulation; multiply and square root against golden hashes of the instructor's model |
| `fixed` | `op1` directed, a 64 x 64 grid and 1,024 random pairs with pass counts; `op2`; `op3` against 64-bit arithmetic; `q_mul` directed, a 64 x 64 grid and random pairs against a 16-bit partial-product reference, algebraic laws, tie rounding | sums and pass counts against golden hashes; the rest against independent references |
| `rubik` | 59 states from distance 0 to the diameter 11: the text parses, applying it solves the cube, its length equals the exact distance; all nine single moves; an 11-move chain; 13 invalid states; every buffer size around the exact fit | the cube model is rewritten in the test from the published face tables |

Every ELF also runs the shared tests: `__mulsi3` (exhaustive below 128, edges, random), `memcpy`, `memset`,
`memmove`, `memcmp`, `strlen`, the printer (`%d`, `%u`, `%lld`, `%llu`, `%x`, `%p`, truncation), the startup code
(zeroed `.bss`, loaded `.data`, 16-byte stack alignment), the counters, and the assertion macros of micro-test. A
build that depends on libgcc, for example through `__divsi3` or `__muldi3`, fails to link, and `nm -u` is part of
the check.

A golden hash reports the block it covers (a row of a grid, a range of 256 or 1,024 inputs), not the single input;
reproduce it with your own reference to find the exact one. `tests/t<program>.c` is portable: the assembly
cross-checks compile only for RISC-V, and the host runner reuses the file, which is how the C versions get the
sanitizers. Keep the C implementations free of inline assembly and RISC-V-only constructs.

## Cycle analysis

Each timed region runs a function over a fixed array of inputs, once for the C version and once for the
assembly, and prints a line such as

```
@@bench prog=gcd fn=gcd impl=c opt=O2 n=16 cycles=179 instret=179 overhead=100 overhead_instret=100 checksum=0
```

`overhead` is the same loop with the call replaced by a copy of its input, so `cycles - overhead` over `n` is the
cost of the function alone. The checksum folds the results, which keeps the optimizer from deleting the work and
lets the driver verify that the C and assembly versions agree.

rv32emu has no timing model. `cycle` and `instret` are one counter of executed guest instructions, so the figures
are instruction counts, and they say nothing about pipeline stalls, cache misses or multi-cycle instructions. The
counts are deterministic: one run per input suffices.

`make cycles` writes `results/cycles.csv`, `footprint.csv`, `cycles.md` and, with matplotlib, three charts:

* `cycles-per-call.png`: cycles per call for `-O1`, `-O2`, `-O3`, `-Os` and the assembly, side by side
* `speedup.png`: the best C build against the assembly, measured in the same ELF
* `footprint.png`: `.text`, `.rodata`, `.data` and `.bss` of each implementation, harness excluded

The assembly is the same object in every ELF, but its caller is compiled at the ELF's optimization level, so its
measured cost moves by a fraction of an instruction between builds. The tables report the assembly from the `-O2`
ELF, and the speedup always compares two functions measured in one ELF.

## Git hooks

`make` installs four hooks that run on every commit and push (`scripts/install-git-hooks`):

* `pre-commit`: workspace path, merge-conflict markers, binary files, host tests under AddressSanitizer and UBSan,
  `clang-format-20`, banned functions (`gets`, `sprintf`, `strcpy`, `strcat`), Cppcheck, non-ASCII file names
* `commit-msg` and `prepare-commit-msg`: the commit message rules of `CONTRIBUTING.md`, with a spell check
* `pre-push`: a clean build and the sanitizer run before a push to `main`

Do not bypass them with `--no-verify`.

## Submission

The directory layout of a submission and the way to hand it in will be announced on the assignment page.

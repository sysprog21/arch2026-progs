# arch2026-progs

Grading framework for [Assignment 2: Complete Applications](https://hackmd.io/@sysprog/2026-arch-homework2).
Four bare-metal RV32I programs run on [rv32emu](https://github.com/sysprog21/rv32emu) in system emulation, each
with a C version, a hand-written assembly version and a micro-test suite. The repository holds the Makefile, the
runtime, the test driver, the cycle analysis and the Git hooks. The programs themselves are stubs to be filled in.

| Program | Source | Assignment item |
|---|---|---|
| `rubik` | 59 states from distance 0 to the diameter 11, one test per distance: the text parses, applying it solves the cube, its length equals the exact distance, and one call retires at most `RUBIK_INSN_BUDGET` instructions; all nine single moves; an 11-move chain; 13 invalid states; every buffer size around the exact fit | the cube model is rewritten in the test from the published face tables; the instruction budget is the level of weiso131/minirubik |
| `bf16` | Quiz 1 Problem B, BFloat16 | 3 |
| `gcd` | Quiz 2 Problem A, binary GCD | 3 |
| `fixed` | Quiz 3 Problem C, bitwise adders and Q11.20 fixed-point multiply | 3 |

## Prerequisites

* A RISC-V GNU toolchain with `riscv-none-elf-`, `riscv32-unknown-elf-` or `riscv64-unknown-elf-` as the prefix
  (for example [xPack](https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack)), GCC 12 or later.
  Set `CROSS_COMPILE=<prefix>` to choose one explicitly.
* The tools rv32emu needs for system emulation: a host C compiler, `make`, `git`, `curl`, and the device tree
  compiler (`dtc`). rv32emu is built without SDL.
* Python 3.9 or later. `matplotlib` is needed only for the charts.
* The [GitHub CLI](https://cli.github.com) `gh`, logged in with `gh auth login`, when your repository is a fork (see
  "GitHub Actions" below). `make` stops without it.
* For the Git hooks: `clang-format-20`, `cppcheck`, `aspell` with the English dictionary.

## Quick start

```shell
make              # install hooks, validate the toolchain, fetch and build the tools, build the programs at -O2
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
| `make` | Install the hooks, verify the history, validate the toolchain, build rv32emu, and build every program at `-O2` (`make OPT=Os` picks another level) |
| `make elfs` | Build `build/<program>/<opt>/<program>.elf` for `-O1`, `-O2`, `-O3` and `-Os`; `make test`, `make cycles`, `make dump` and `make size` do so themselves |
| `make neg` | Build the negative control, whose failing test must make `main` return nonzero |
| `make test` | `make repo` (hooks installed, history, commit messages) and `make grade` |
| `make grade` | Build every ELF and run `scripts/driver.py`, which grades every item below (`scripts/grade.sh` does so with a clean harness, see Grading) |
| `make unittest` | Unit tests of the driver's static-data accounting (`scripts/testsections.py`) |
| `make emu-rev` | Print the latest rv32emu commit, for cache keys |
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
| `rubik`, `bf16`, `gcd`, `fixed` | 2 and 3: per optimization level, only RV32I and the Zicsr counter reads, no undefined symbols outside the allowed runtime routines, every micro-test passes, `main` returns 0, at least twenty tests, C and assembly agree on the benchmark inputs, the instruction budgets of the C and the assembly versions hold, the assembly takes at most the best C build's cycles per call plus 2, and the rubik data (every allocated section) stays within `RUBIK_DATA_BUDGET` | 15 each |
| `disassembly` | 4: listings exist for every C build and for the assembly | 5 |
| `cycles` | 5: each function has cycle data for the four C builds and the assembly | 10 |
| `footprint` | 5: `size -A` works on every implementation | 5 |
| `hygiene` | no unsafe functions, no `-Ofast`, no non-RV32I `-march` | 5 |

An untouched skeleton scores 0: the items that are not a program (toolchain, emulator, listings, cycle data, sizes,
hygiene) pass for any skeleton, so each counts in proportion to the programs that pass.

`make test` also checks that the Git hooks are installed and that every commit follows the commit rules. Quality
of the HackMD note, the optimization results and the CodeTrial mock interview are reviewed by hand and are not
part of the script. Run `scripts/driver.py --help` for the options, for example `--prog gcd` and `--min-tests`.

The skeleton starts red on purpose: the dummy implementations return constants, so no program passes until it is written.

## Layout

```
Makefile, mk/        build rules; toolchain detection; rv32emu fetch and build
common/              start.S, linker.ld, perf.S, runtime helpers (rt), printer (print),
                     micro-test (utest.h, utest.c), benchmark macros (bench.h)
src/<program>/       <program>.h (the contract), <program>.c (dummy), <program>.S (stub); yours to extend
tests/               t<program>.c, <program>vec.h (golden vectors), trt.c (runtime, printer,
                     startup), tutest.c (micro-test), tutil.h, host.c, neg.c
scripts/             driver.py, cycles.py, rvcheck.py, toolchain.sh, benchlib.py, Git hooks
results/             generated cycle tables and charts
tools/hashchain.c    the hash chain of Change-Ids (SHA-256, no library)
tools/oracle/        instructor-only reference models that generate tests/*vec.h (ignored by Git)
```

Each ELF links both implementations. The C version has the function's own name and the assembly version carries
an `_asm` suffix, so one test binary cross-checks them. `tests/m<program>.c` registers the tests, runs them, then prints the
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

Performance is tested, not only correctness, and it is the assembly that is held to account: the deliverable is
highly optimized RV32I assembly, and the C version is the step on the way. Every function has two instruction
budgets in its contract header, a tight one for the assembly (`RUBIK_INSN_BUDGET`, `GCD_INSN_BUDGET`,
`BF16_*_INSN_BUDGET`, `FIXED_*_INSN_BUDGET`) and a loose one for the C version (the same names with `_C_`). The tests
count the worst call over the whole operand space with the `instret` counter and print it as `@@perf`. rv32emu counts
executed instructions, so a budget is exact. `scripts/driver.py` adds three checks that a correct program can still
fail:

* `RUBIK_DATA_BUDGET` (128 KiB): every allocated section of each rubik implementation, code included, by `size -A`,
  so the name of the section a table sits in does not matter
* the assembly of each function takes no more cycles per call (plus 2 for measurement noise) than the best of the four
  compiler builds, measured in the same ELF: assembly that does not beat the compiler has not done its job
* a run that exceeds `--timeout` seconds (default 120) is a failure, so a solver that does not finish does not hang the
  grader

The rubik budget of the assembly is calibrated on weiso131/minirubik: iterative deepening that ends in a binary-search
table of the states within 5 moves and a table of the states at distance 11. Its C version retires at most 7.5 million
instructions for the most expensive of all 3,674,160 states and keeps 67 KB of data, and its assembly less; the budget
is 7.5 million. The C version only has to meet the budget of the assignment, 50 million. A plain breadth-first table
(18 MB, about 10^9 instructions) fails both; a plain IDA* with permutation and orientation distances (above 7.5
million at distance 11) passes as C and fails as assembly.

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

`make` installs five hooks on its first run (`scripts/install-git-hooks`, the same way lab0-c does) and runs them on
every commit and push:

* `pre-commit`: workspace path, merge-conflict markers, binary files, host tests under AddressSanitizer and UBSan,
  `clang-format-20`, banned functions (`gets`, `sprintf`, `strcpy`, `strcat`), Cppcheck, non-ASCII file names
* `commit-msg` and `prepare-commit-msg`: the commit message rules of `CONTRIBUTING.md`, with a spell check, and the
  `Change-Id` of the hash chain; `post-commit` repairs that id after an amend
* `pre-push`: the hash chain on every push, and a clean build and the sanitizer run before a push to `main`

Do not bypass them with `--no-verify`.

## Commit history

Every commit since the base commit (`75c966c`) must carry a `Change-Id` that continues a hash chain. The id is the
first 40 hex digits of SHA-256 over the previous id and the tree of the commit, and the `commit-msg` hook computes it
when you commit; `tools/hashchain.c` implements the hash without any library. `make`, `make test`, `git push` and the
GitHub Actions workflow recompute the chain from `git log` (`make history`). A commit made with `--no-verify` has no
Change-Id, so the first such commit is reported by name and the next commit is refused until the chain is repaired.

* History must be linear: no merge commits since the base commit. `make` sets `pull.rebase` for the repository, so
  `git pull` rebases. Do not merge, and do not use the GitHub web editor, "Update branch", "Sync fork" or a squash
  merge: they create commits without a link.
* `git commit --amend` is handled: the `post-commit` hook restamps the amended commit when the `commit-msg` hook had to
  guess.
* A rebase, a squash, a cherry-pick, a revert and a commit made without the hooks leave links that do not fit. Repair
  them with `scripts/chain.sh rechain`, then push with `git push --force-with-lease`. It restamps only the commits
  whose link is wrong, keeps the others (and their hashes), keeps uncommitted changes, and adds a `Rechained: yes`
  trailer to each commit it rewrote; `make history` counts them.
* `make chaintest` exercises all of this in a scratch repository.

What the chain does and does not show. It shows that every commit passed through `commit-msg` (or was restamped by
`rechain`, visibly) and that the sequence of trees was not edited afterwards. It does not cover the message text
alone, a force-push back to an earlier valid commit, or the checks of the other hooks; and anyone who runs the tool by
hand can forge a link. The fork's CI verifies with the fork's own scripts and, in a second step, with the scripts of
`sysprog21/arch2026-progs`; grading uses the instructor's copy (`<instructor>/scripts/chain.sh verify` run inside the
fork), so a student-edited verifier does not count. Linked Git worktrees are not supported.

## GitHub Actions

GitHub disables the workflows of a new fork, so a regression would never reach your e-mail. When the `origin` remote
is not `sysprog21/arch2026-progs`, the first `make` therefore requires `gh` and checks, in this order:

* `gh` is installed and logged in; otherwise `make` stops with the instruction to install it or run `gh auth login`.
* The repository is a fork of `sysprog21/arch2026-progs`; otherwise `make` stops.
* Its default branch is `main`; otherwise `make` warns and prints `gh repo edit <repo> --default-branch main`, since
  the course grades `main`.
* Actions is allowed for the repository and every disabled workflow is enabled, with `gh api` and `gh workflow
  enable`.

The check is skipped inside GitHub Actions and in a clone without an `origin` remote. It runs once, together with the
hook installation; a failed check leaves the hooks uninstalled, so the next `make` repeats it.

## Grading

`scripts/grade.sh` is the one entry point for CI, the instructor and a student. The harness is everything but the
implementations: `tests/` (the test programs, the main program of each ELF, the vectors), `common/`, `scripts/`, `mk/`,
the Makefile, and the `src/<prog>/<prog>.h` contracts (they hold the budgets). A submission is the rest of
`src/<prog>/`: C and assembly sources, tables and headers of its own (every `src/<prog>/*.c` and `*.S` is built, and
the pieces are joined with `ld -r` into the one object that is measured). The script copies the harness of the checkout
it lives in to a scratch tree, replaces the sources of each program with the submission's (links are dropped), and runs
the host tests under sanitizers, the build and `scripts/driver.py` there. Editing a test, a budget, a vector or the
driver in a fork therefore changes nothing, and since the harness reaches a contract by its path, a header of the
submission cannot take the place of a harness or system header. The emulator is built from the same checkout's rules,
without the M, A, F and C extensions: it cannot execute a multiply however the encoding is smuggled in.

```shell
scripts/grade.sh                                  # your own checkout, as `make grade` does
.trusted/scripts/grade.sh --submission . --chain  # a fork, with the instructor's harness and the hash chain
```

`--chain` first verifies the Change-Id chain of the submission's history, which needs the full history and committed
sources. `--secret DIR` copies the files of `DIR` over `tests/`: the instructor's private vectors from
`tools/oracle/gen.py --seed N --out DIR`, which a student cannot have memorized (the published vectors fix the cube
states of rubik and the probe patterns of bf16). The grader reports the same score in CI and on the instructor's
machine; the fork's workflow is feedback, and a student who removes a step from it only removes the feedback.

The implementation objects are checked on their own (`scripts/rvcheck.py --impl`): no `ecall`, `ebreak`, fence or CSR
access, which belong to the harness, and no reference to anything outside them. The assembly may call only `__mulsi3`,
the C version also the string routines GCC emits, so an assembly function that merely jumps to the C one is rejected.
Assembly pasted from compiler output passes if it is as fast as the best compiler build; the grader cannot tell it
from hand-written assembly, and the notes of the assignment, not the script, say what is expected.

What the harness cannot do is make adversarial guest code honest: all of it shares one address space, and its output is
its own report. The checks above remove the ways to print, exit or touch the counters, and the code is reviewed. If
that is not enough, counters and results have to be collected from the emulator's side.

## Continuous integration

`.github/workflows/main.yml` runs three jobs in parallel and an aggregate job, `ci`, which is the one to read:

| Job | What it checks | Time |
|---|---|---|
| `lint` | trailing newlines; the unit tests of the scripts; `clang-format` 20.1.8 | seconds |
| `chain` | the hash chain, first with the scripts of `sysprog21/arch2026-progs` (a missing verifier fails), then with the submission's own; `make chaintest` when `scripts/` or `tools/` differ from the instructor's | under a minute, a minute more with `chaintest` |
| `grade` | `scripts/grade.sh` with the instructor's harness from `main`: host tests under sanitizers, the RV32I build and the micro-test suites at all four levels, the cycle charts as an artifact | minutes |

A push to any branch runs the jobs once, a pull request from a fork runs them, and a pull request from a branch of the
same repository is skipped because its push run covers the same commit. A newer push cancels the older run, except on
`main`. The cross toolchain (with its published SHA-256) and the latest rv32emu are cached and saved as soon as they
are built, so a failing grade does not lose them; the rv32emu key is its resolved upstream commit, so a new upstream
commit rebuilds it. Failure e-mails come from this workflow, which is why the first `make` enables Actions on a fork.

## Submission

The directory layout of a submission and the way to hand it in will be announced on the assignment page.

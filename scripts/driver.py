#!/usr/bin/env python3
"""Grade the assignment items against the built ELF files.

Every check maps to a requirement of the assignment page:

  toolchain    GNU toolchain accepts -march=rv32i_zicsr, '*' becomes __mulsi3
  emulator     rv32emu was built with system emulation and ELF_LOADER, and
               its exit status carries main's return value (negative control)
  <program>    items 2 and 3: per optimization level, the ELF obeys the
               RV32I-only rule, every micro-test passes (main returns 0, at
               least --min-tests tests, the instruction budgets of the
               assembly and, looser, of the C version included), and the C and
               assembly versions agree on the benchmark inputs; the assembly
               must not take more cycles than the best C build, because the
               deliverable is optimized assembly and C is the step on the way
  disassembly  item 4: listings exist for the C builds and the assembly
  cycles       item 5: each function has cycle data for -O1 -O2 -O3 -Os and asm
  footprint    item 5: size -A is available per implementation

Scoring. A program is worth its own points. The other items pass for any
skeleton, so each counts in proportion to the programs that pass: an
untouched skeleton scores 0, and one program in four earns a quarter of them.

Run `make grade`; this script expects the ELF files under build/ already.
"""

import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from benchlib import BenchError, find_bench_lines, run_elf, static_data  # noqa: E402
from cycles import best_ratio, per_call_table, report  # noqa: E402

PROGS = ["rubik", "bf16", "gcd", "fixed"]
# The functions each ELF must benchmark, for the C and the assembly version.
# src/<prog>/main.c is the instructor's, so a missing or extra line is a defect.
EXPECTED_FNS = {
    "rubik": {"solve"},
    "bf16": {"from_f32", "mul", "sqrt"},
    "gcd": {"gcd"},
    "fixed": {"op1", "op2", "op3", "q_mul"},
}
# Programs whose contract header must define <PROG>_DATA_BUDGET.
DATA_BUDGET_PROGS = {"rubik"}
OPTS = ["O1", "O2", "O3", "Os"]
POINTS = {
    "toolchain": 5,
    "emulator": 10,
    "rubik": 15,
    "bf16": 15,
    "gcd": 15,
    "fixed": 15,
    "disassembly": 5,
    "cycles": 10,
    "footprint": 5,
    "hygiene": 5,
}

RED, GREEN, YELLOW, NC = "\033[1;31m", "\033[1;32m", "\033[1;33m", "\033[0m"
if not sys.stdout.isatty():
    RED = GREEN = YELLOW = NC = ""


class Result:
    def __init__(self, name):
        self.name = name
        self.problems = []
        self.notes = []

    def fail(self, msg):
        self.problems.append(msg)

    def note(self, msg):
        self.notes.append(msg)

    @property
    def ok(self):
        return not self.problems


def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, check=False, **kw)


def check_toolchain(ctx):
    r = Result("toolchain")
    p = sh(["scripts/toolchain.sh", ctx.prefix])
    if p.returncode != 0:
        r.fail((p.stdout + p.stderr).strip().splitlines()[-1])
    return r


def check_emulator(ctx):
    r = Result("emulator")
    cfg = os.path.join(os.path.dirname(ctx.emu), ".effective-config")
    if not os.path.exists(ctx.emu):
        r.fail(f"{ctx.emu} is missing (make deps)")
        return r
    try:
        text = open(cfg).read()
    except OSError:
        r.fail(f"{cfg} is missing; rebuild the emulator with make deps")
        return r
    for want in ("CONFIG_SYSTEM=y", "CONFIG_ELF_LOADER=y"):
        if not re.search("^" + re.escape(want) + "$", text, re.M):
            r.fail(f"rv32emu lacks {want}")
    # The emulator must not know M, A, F or C: an encoding hidden in data and
    # jumped to would pass any scan of the instructions, and would multiply.
    for ext in ("M", "A", "F", "C"):
        if re.search(f"^CONFIG_EXT_{ext}=y$", text, re.M):
            r.fail(f"rv32emu was built with the {ext} extension; rebuild it (make update)")
    neg = os.path.join(ctx.build, "neg", "neg.elf")
    if not os.path.exists(neg):
        r.fail(f"{neg} is missing (make neg)")
        return r
    rc, out = run_elf(ctx.emu, neg)
    if "neg_passes ... ok" not in out:
        r.fail("negative control: the passing test did not report ok")
    if "neg_fails ... " not in out or "failed" not in out:
        r.fail("negative control: the failing test did not show in the output")
    if rc == 0:
        r.fail("negative control: main returned 0 although a test failed")
    return r


def failed_tests(out):
    """Names of the tests whose block in the micro-test output ends in 'failed'."""
    names, current = [], None
    for line in out.splitlines():
        m = re.match(r"^(\S+) \.\.\. ", line)
        if m:
            current = m.group(1)
        if current and line.endswith("failed"):
            names.append(current)
            current = None
        elif line.endswith(" ok") or line.startswith("Summary:"):
            current = None
    return names


def header_budget(prog, name):
    """Value of `#define <PROG>_<name> (<n> * <m>)` or `<n>` in the contract header."""
    text = open(f"src/{prog}/{prog}.h").read()
    m = re.search(rf"#define {prog.upper()}_{name}\s+\(?\s*(\d+)(?:\s*\*\s*(\d+))?", text)
    if not m:
        return None
    return int(m.group(1)) * int(m.group(2) or 1)


def check_data_budget(ctx, prog, r):
    """Each implementation's static data must fit the contract.

    Every allocated section except code and debug information counts, whatever
    its name. A contract that should state a budget and does not is a failure,
    never a reason to skip the check.
    """
    budget = header_budget(prog, "DATA_BUDGET")
    if budget is None:
        if prog in DATA_BUDGET_PROGS:
            r.fail(f"src/{prog}/{prog}.h defines no {prog.upper()}_DATA_BUDGET")
        return
    objs = [("assembly", os.path.join(ctx.build, prog, "impl_asm.o"))]
    objs += [(f"C -{o}", os.path.join(ctx.build, prog, o, "impl.o")) for o in OPTS]
    for label, path in objs:
        if not os.path.exists(path):
            continue
        used = static_data(sh([ctx.prefix + "size", "-A", path]).stdout)
        if used > budget:
            r.fail(f"{label}: {used} bytes of static data, the budget is {budget}")


def check_speed(rows, prog, r, slack=2.0):
    """The assembly must match or beat the best compiler build, per function.

    Both are measured in one ELF, so the harness is identical; the best C build
    is the one with the fewest cycles per call. The slack absorbs the fraction
    of an instruction by which the caller's code differs between builds.
    """
    for (p, fn), cell in sorted(per_call_table([x for x in rows if x["prog"] == prog]).items()):
        c = {v: cell[v] for v in ("-O1", "-O2", "-O3", "-Os") if v in cell}
        if not c:
            continue
        best = min(c, key=c.get)
        asm = cell.get("asm@" + best)
        if asm is not None and asm > c[best] + slack:
            r.fail(f"{fn}: the assembly takes {asm:.1f} cycles per call, the best C build ({best}) {c[best]:.1f}")


def check_program(ctx, prog):
    r = Result(prog)
    sums = {}
    rows = []
    for opt in OPTS:
        elf = os.path.join(ctx.build, prog, opt, f"{prog}.elf")
        if not os.path.exists(elf):
            r.fail(f"-{opt}: {elf} is missing")
            continue
        isa = sh([sys.executable, "scripts/rvcheck.py", "--prefix", ctx.prefix, elf])
        if isa.returncode != 0:
            r.fail(f"-{opt}: RV32I-only rule violated: " + " | ".join(l.strip() for l in isa.stdout.splitlines()[1:4]))
        rc, out = run_elf(ctx.emu, elf, ctx.timeout)
        if rc is None:
            r.fail(f"-{opt}: no result after {ctx.timeout} s: a hang, or an implementation far too slow")
            break
        m = re.search(r"Summary: (\d+) passed, (\d+) failed", out)
        if not m:
            r.fail(f"-{opt}: no micro-test summary (rc={rc})")
            continue
        passed, failed = int(m.group(1)), int(m.group(2))
        if failed:
            names = failed_tests(out)
            r.fail(f"-{opt}: {failed} test(s) failed: " + ", ".join(names[:6]) + (" ..." if len(names) > 6 else ""))
        elif passed < ctx.min_tests:
            r.fail(f"-{opt}: only {passed} test(s); at least {ctx.min_tests} required")
        if rc != 0:
            r.fail(f"-{opt}: main returned {rc}, expected 0")
        try:
            got = find_bench_lines(out)
        except BenchError as e:
            r.fail(f"-{opt}: {e}")
            continue
        have = sorted((b["prog"], b["fn"], b["impl"], b["opt"]) for b in got)
        want = sorted((prog, fn, impl, opt) for fn in EXPECTED_FNS[prog] for impl in ("c", "asm"))
        if have != want:
            r.fail(f"-{opt}: the benchmark lines are not exactly {len(want)} (one per function and version): got {len(have)}")
            continue
        for b in got:
            sums.setdefault((b["fn"], b["opt"]), {})[b["impl"]] = b["checksum"]
            rows.append(b)
    # The implementations on their own: no environment call or counter access,
    # and no reference to anything but the allowed runtime routines. An
    # assembly function that just jumps to the C one fails here.
    objs = [("assembly", "asm", os.path.join(ctx.build, prog, "impl_asm.o"))]
    objs += [(f"C -{o}", "c", os.path.join(ctx.build, prog, o, "impl.o")) for o in OPTS]
    for label, kind, path in objs:
        if not os.path.exists(path):
            continue
        isa = sh([sys.executable, "scripts/rvcheck.py", "--prefix", ctx.prefix, "--impl", kind, path])
        if isa.returncode != 0:
            r.fail(f"{label}: " + " | ".join(l.strip() for l in isa.stdout.splitlines()[1:4]))
    for (fn, opt), by_impl in sorted(sums.items()):
        if set(by_impl) != {"c", "asm"}:
            r.fail(f"{fn} -{opt}: benchmark lacks the {'asm' if 'c' in by_impl else 'c'} run")
        elif by_impl["c"] != by_impl["asm"]:
            r.fail(f"{fn} -{opt}: C and assembly checksums differ ({by_impl['c']} vs {by_impl['asm']})")
    check_data_budget(ctx, prog, r)
    check_speed(rows, prog, r)
    ctx.rows += rows
    return r


def check_disassembly(ctx):
    r = Result("disassembly")
    for prog in PROGS:
        paths = [os.path.join(ctx.build, prog, "asm.dis")]
        paths += [os.path.join(ctx.build, prog, o, f"{prog}.dis") for o in OPTS]
        for p in paths:
            if not os.path.exists(p) or os.path.getsize(p) == 0:
                r.fail(f"{p} is missing (make dump)")
    return r


def check_cycles(ctx, progs):
    """Cycle data for every function, from the runs check_program already made."""
    r = Result("cycles")
    rows = ctx.rows
    for prog in progs:
        for fn in sorted(EXPECTED_FNS[prog]):
            for impl in ("c", "asm"):
                missing = [o for o in OPTS if not any(x["prog"] == prog and x["fn"] == fn and x["impl"] == impl and x["opt"] == o for x in rows)]
                if missing:
                    r.fail(f"{prog}.{fn} ({impl}): no cycle data for {', '.join('-' + o for o in missing)}")
    # Informational: does the assembly match or beat the best C build?
    for (prog, fn), cell in sorted(per_call_table(rows).items()):
        ratio = best_ratio(cell)
        if ratio is not None:
            tag = "beats" if ratio > 1.005 else ("matches" if ratio >= 0.995 else "is slower than")
            r.note(f"{prog}.{fn}: assembly {tag} the best C build ({ratio:.2f}x)")
    return r


def check_footprint(ctx):
    r = Result("footprint")
    for prog in PROGS:
        objs = [os.path.join(ctx.build, prog, "impl_asm.o")]
        objs += [os.path.join(ctx.build, prog, o, "impl.o") for o in OPTS]
        for o in objs:
            p = sh([ctx.prefix + "size", "-A", o])
            if p.returncode != 0 or ".text" not in p.stdout:
                r.fail(f"size -A failed on {o}")
    return r


BANNED = re.compile(r"(?<![\w])(?:gets|sprintf|strcpy|strcat)\(")


def check_hygiene(ctx):
    """No unsafe function in the sources of the submission, no -Ofast, no wide -march."""
    r = Result("hygiene")
    for prog in PROGS:
        d = os.path.join("src", prog)
        for name in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            if not name.endswith((".c", ".h")):
                continue
            for n, line in enumerate(open(os.path.join(d, name), errors="replace"), 1):
                if BANNED.search(line):
                    r.fail(f"{d}/{name}:{n}: unsafe function")
    mk = open("Makefile").read() + open("mk/toolchain.mk").read()
    if re.search(r"-Ofast|-march=rv32[a-z]*[mfdc][^ ]*", mk.replace("rv32i_zicsr", "")):
        r.fail("the build flags select -Ofast or a non-RV32I -march")
    return r


class Ctx:
    pass


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--emu", default="third_party/rv32emu/build/rv32emu")
    ap.add_argument("--build", default="build")
    ap.add_argument("--prefix", default=os.environ.get("CROSS_COMPILE") or sh(["scripts/toolchain.sh", "--detect"]).stdout.strip())
    ap.add_argument("--timeout", type=int, default=120, help="seconds one ELF may run (default 120)")
    ap.add_argument("--min-tests", type=int, default=20, help="micro-tests required per program (default 20)")
    ap.add_argument("--prog", action="append", choices=PROGS, help="limit to one program (repeatable)")
    ap.add_argument("--quick", action="store_true", help="skip the cycle analysis")
    ap.add_argument("--out", help="write the cycle tables and charts here, from the runs the grading made")
    ctx = ap.parse_args(namespace=Ctx())
    ctx.rows = []

    progs = ctx.prog or PROGS
    checks = [("toolchain", check_toolchain), ("emulator", check_emulator)]
    checks += [(p, lambda c, p=p: check_program(c, p)) for p in progs]
    checks += [("disassembly", check_disassembly)]
    if not ctx.quick:
        checks += [("cycles", lambda c: check_cycles(c, progs))]
    checks += [("footprint", check_footprint), ("hygiene", check_hygiene)]

    results = [(name, fn(ctx)) for name, fn in checks]
    if ctx.out and ctx.rows:
        names = report(ctx.rows, ctx.build, ctx.out, ctx.prefix)
        print(f"--- wrote {ctx.out}/" + ", ".join(names))

    # An untouched skeleton must score zero. The items that are not a program
    # (toolchain, emulator, listings, cycle data, sizes, hygiene) pass for any
    # skeleton, so they count in proportion to the programs that pass.
    passed = sum(1 for name, res in results if name in progs and res.ok)
    share = passed / len(progs)

    score = total = 0.0
    for name, res in results:
        pts = POINTS[name]
        got = pts if name in progs else pts * share
        total += pts
        mark = f"{GREEN}PASS{NC}" if res.ok else f"{RED}FAIL{NC}"
        earned = got if res.ok else 0
        print(f"--- {name:<12} {mark}  {earned:>5.1f}/{pts}")
        for p in res.problems:
            print(f"    {RED}x{NC} {p}")
        for n in res.notes:
            print(f"    {YELLOW}-{NC} {n}")
        score += earned
    print(f"--- {passed} of {len(progs)} programs pass; the other items count {share:.0%}")
    print(f"--- TOTAL {score:.1f}/{total:.0f}")
    return 0 if passed == len(progs) and all(res.ok for _, res in results) else 1


if __name__ == "__main__":
    sys.exit(main())

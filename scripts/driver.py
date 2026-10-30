#!/usr/bin/env python3
"""Grade the assignment items against the built ELF files.

Every check maps to a requirement of the assignment page:

  toolchain    GNU toolchain accepts -march=rv32i_zicsr, '*' becomes __mulsi3
  emulator     rv32emu was built with system emulation and ELF_LOADER, and
               its exit status carries main's return value (negative control)
  <program>    items 2 and 3: per optimization level, the ELF obeys the
               RV32I-only rule, every micro-test passes (main returns 0, at
               least --min-tests tests), and the C and assembly versions
               agree on the benchmark inputs
  disassembly  item 4: listings exist for the C builds and the assembly
  cycles       item 5: each function has cycle data for -O1 -O2 -O3 -Os and asm
  footprint    item 5: size -A is available per implementation

Run `make test`; this script expects the ELF files under build/ already.
"""

import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from benchlib import collect, find_bench_lines, run_elf  # noqa: E402
from cycles import best_ratio, per_call_table  # noqa: E402

PROGS = ["rubik", "bf16", "gcd", "fixed"]
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


def check_program(ctx, prog):
    r = Result(prog)
    sums = {}
    for opt in OPTS:
        elf = os.path.join(ctx.build, prog, opt, f"{prog}.elf")
        if not os.path.exists(elf):
            r.fail(f"-{opt}: {elf} is missing")
            continue
        isa = sh([sys.executable, "scripts/rvcheck.py", "--prefix", ctx.prefix, elf])
        if isa.returncode != 0:
            r.fail(f"-{opt}: RV32I-only rule violated: " + " | ".join(l.strip() for l in isa.stdout.splitlines()[1:4]))
        rc, out = run_elf(ctx.emu, elf)
        m = re.search(r"Summary: (\d+) passed, (\d+) failed", out)
        if not m:
            r.fail(f"-{opt}: no micro-test summary (rc={rc})")
            continue
        passed, failed = int(m.group(1)), int(m.group(2))
        if failed:
            r.fail(f"-{opt}: {failed} test(s) failed")
        elif passed < ctx.min_tests:
            r.fail(f"-{opt}: only {passed} test(s); at least {ctx.min_tests} required")
        if rc != 0:
            r.fail(f"-{opt}: main returned {rc}, expected 0")
        for b in find_bench_lines(out):
            sums.setdefault((b["fn"], b["opt"]), {})[b["impl"]] = b["checksum"]
    asm_obj = os.path.join(ctx.build, prog, "impl_asm.o")
    if os.path.exists(asm_obj):
        isa = sh([sys.executable, "scripts/rvcheck.py", "--prefix", ctx.prefix, asm_obj])
        if isa.returncode != 0:
            r.fail("assembly object: " + " | ".join(l.strip() for l in isa.stdout.splitlines()[1:4]))
    for (fn, opt), by_impl in sorted(sums.items()):
        if set(by_impl) != {"c", "asm"}:
            r.fail(f"{fn} -{opt}: benchmark lacks the {'asm' if 'c' in by_impl else 'c'} run")
        elif by_impl["c"] != by_impl["asm"]:
            r.fail(f"{fn} -{opt}: C and assembly checksums differ ({by_impl['c']} vs {by_impl['asm']})")
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


def check_cycles(ctx):
    r = Result("cycles")
    rows = collect(ctx.emu, ctx.build, PROGS, OPTS)
    seen = {}
    for row in rows:
        seen.setdefault((row["prog"], row["fn"]), set()).add((row["impl"], row["opt"]))
    for (prog, fn), have in sorted(seen.items()):
        want = {("c", o) for o in OPTS} | {("asm", o) for o in OPTS}
        missing = want - have
        if missing:
            r.fail(f"{prog}.{fn}: missing {sorted(missing)}")
    if not rows:
        r.fail("no benchmark lines were produced")
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


BANNED = re.compile(r"([^f]gets\()|(sprintf\()|(strcpy\()|(strcat\()")


def check_hygiene(ctx):
    r = Result("hygiene")
    files = sh(["git", "ls-files", "src", "common", "tests"]).stdout.split()
    for f in files:
        if not f.endswith((".c", ".h")) or not os.path.exists(f):
            continue
        for n, line in enumerate(open(f, errors="replace"), 1):
            if BANNED.search(line):
                r.fail(f"{f}:{n}: unsafe function")
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
    ap.add_argument("--min-tests", type=int, default=20, help="micro-tests required per program (default 20)")
    ap.add_argument("--prog", action="append", choices=PROGS, help="limit to one program (repeatable)")
    ap.add_argument("--quick", action="store_true", help="skip the cycle analysis")
    ctx = ap.parse_args(namespace=Ctx())

    progs = ctx.prog or PROGS
    checks = [("toolchain", check_toolchain), ("emulator", check_emulator)]
    checks += [(p, lambda c, p=p: check_program(c, p)) for p in progs]
    checks += [("disassembly", check_disassembly)]
    if not ctx.quick:
        checks += [("cycles", check_cycles)]
    checks += [("footprint", check_footprint), ("hygiene", check_hygiene)]

    score = total = 0
    for name, fn in checks:
        res = fn(ctx)
        pts = POINTS[name]
        total += pts
        mark = f"{GREEN}PASS{NC}" if res.ok else f"{RED}FAIL{NC}"
        print(f"--- {name:<12} {mark}  {pts if res.ok else 0:>3}/{pts}")
        for p in res.problems:
            print(f"    {RED}x{NC} {p}")
        for n in res.notes:
            print(f"    {YELLOW}-{NC} {n}")
        if res.ok:
            score += pts
    print(f"--- TOTAL {score}/{total}")
    return 0 if score == total else 1


if __name__ == "__main__":
    sys.exit(main())

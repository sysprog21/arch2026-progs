"""Shared helpers: run an ELF on rv32emu and parse the @@bench protocol."""

import re
import subprocess

BENCH = re.compile(r"^@@bench (.*)$", re.M)


def run_elf(emu, elf, timeout=120):
    """Return (exit status, output) of one run. rv32emu passes main's status on.

    A run that exceeds the timeout returns (None, output so far): a hang, or an
    implementation far too slow, is a failure to report, not a crash.
    """
    try:
        p = subprocess.run([emu, elf], capture_output=True, text=True, timeout=timeout, check=False)
    except subprocess.TimeoutExpired as e:
        out = e.stdout or b""
        return None, out.decode(errors="replace") if isinstance(out, bytes) else out
    return p.returncode, p.stdout + p.stderr


class BenchError(ValueError):
    """A malformed or inconsistent @@bench line."""


def find_bench_lines(out):
    """Parse every '@@bench key=value ...' line into a dict.

    Raises BenchError for a line that lacks a field, has a non-numeric value,
    has no calls, or reports less than its own loop baseline.
    """
    rows = []
    for m in BENCH.finditer(out):
        try:
            d = dict(kv.split("=", 1) for kv in m.group(1).split())
            for k in ("n", "cycles", "instret", "overhead", "overhead_instret", "checksum"):
                d[k] = int(d[k])
            for k in ("prog", "fn", "impl", "opt"):
                d[k]
        except (KeyError, ValueError) as e:
            raise BenchError(f"bad benchmark line: @@bench {m.group(1)[:120]} ({e})") from e
        if d["n"] <= 0 or d["cycles"] < d["overhead"] or d["instret"] < d["overhead_instret"]:
            raise BenchError(f"inconsistent benchmark line: @@bench {m.group(1)[:120]}")
        d["per_call"] = (d["cycles"] - d["overhead"]) / d["n"]
        d["per_call_instret"] = (d["instret"] - d["overhead_instret"]) / d["n"]
        rows.append(d)
    return rows


def collect(emu, build, progs, opts, timeout=120):
    """Run every <prog>/<opt> ELF; return one row per measured function.

    per_call is (cycles - overhead) / n: the cost of the function alone, with
    the loop control, input load and counter reads removed. rv32emu counts
    executed instructions, so cycles and instret agree by construction.
    """
    rows = []
    for prog in progs:
        for opt in opts:
            _, out = run_elf(emu, f"{build}/{prog}/{opt}/{prog}.elf", timeout)
            rows += find_bench_lines(out)
    return rows


def sections(size_output):
    """Parse `size -A` into {section: bytes} for the four sections of interest."""
    want = {".text": 0, ".rodata": 0, ".data": 0, ".bss": 0}
    for line in size_output.splitlines():
        f = line.split()
        # RISC-V small-data sections count toward the same static-data totals.
        if f:
            f[0] = re.sub(r"^\.s(rodata|data|bss)(?=\.|$)", r".\1", f[0])
        if len(f) >= 2 and f[0] in want and f[1].isdigit():
            want[f[0]] += int(f[1])
        # Sub-sections such as .text.foo or .rodata.cst4 roll into their parent.
        elif len(f) >= 2 and f[1].isdigit():
            for s in want:
                if f[0].startswith(s + "."):
                    want[s] += int(f[1])
    return want


# Sections that take no room in the image: debug information and notes.
_NOT_IMAGE = (".debug", ".comment", ".riscv.attributes", ".note", ".eh_frame", ".rela", ".rel.", ".group")


def static_data(size_output):
    """Bytes of every allocated section of an object, code included.

    This is the figure a data budget is held to. Unlike sections(), which
    reports the four usual sections, it counts a table in any section name
    (.lookup, .rodata.cst8, .sdata) and in .text: a budget must not depend on
    where a table is put.
    """
    total = 0
    for line in size_output.splitlines():
        f = line.split()
        if len(f) < 2 or not f[0].startswith(".") or not f[1].isdigit():
            continue
        if f[0].startswith(_NOT_IMAGE):
            continue
        total += int(f[1])
    return total

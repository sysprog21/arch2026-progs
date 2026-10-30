"""Shared helpers: run an ELF on rv32emu and parse the @@bench protocol."""

import re
import subprocess

BENCH = re.compile(r"^@@bench (.*)$", re.M)


def run_elf(emu, elf, timeout=300):
    """Return (exit status, stdout) of one run. rv32emu passes main's status on."""
    p = subprocess.run([emu, elf], capture_output=True, text=True, timeout=timeout, check=False)
    return p.returncode, p.stdout + p.stderr


def find_bench_lines(out):
    """Parse every '@@bench key=value ...' line into a dict."""
    rows = []
    for m in BENCH.finditer(out):
        d = dict(kv.split("=", 1) for kv in m.group(1).split())
        for k in ("n", "cycles", "instret", "overhead", "overhead_instret", "checksum"):
            d[k] = int(d[k])
        rows.append(d)
    return rows


def collect(emu, build, progs, opts):
    """Run every <prog>/<opt> ELF; return one row per measured function.

    per_call is (cycles - overhead) / n: the cost of the function alone, with
    the loop control, input load and counter reads removed. rv32emu counts
    executed instructions, so cycles and instret agree by construction.
    """
    rows = []
    for prog in progs:
        for opt in opts:
            _, out = run_elf(emu, f"{build}/{prog}/{opt}/{prog}.elf")
            for d in find_bench_lines(out):
                d["per_call"] = (d["cycles"] - d["overhead"]) / d["n"]
                d["per_call_instret"] = (d["instret"] - d["overhead_instret"]) / d["n"]
                rows.append(d)
    return rows


def sections(size_output):
    """Parse `size -A` into {section: bytes} for the four sections of interest."""
    want = {".text": 0, ".rodata": 0, ".data": 0, ".bss": 0}
    for line in size_output.splitlines():
        f = line.split()
        if len(f) >= 2 and f[0] in want and f[1].isdigit():
            want[f[0]] += int(f[1])
        # Sub-sections such as .text.foo or .rodata.cst4 roll into their parent.
        elif len(f) >= 2 and f[1].isdigit():
            for s in want:
                if f[0].startswith(s + "."):
                    want[s] += int(f[1])
    return want

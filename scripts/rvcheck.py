#!/usr/bin/env python3
"""Verify that ELF files and objects obey the RV32I-only rule.

A file passes when
  * every instruction in its executable sections is RV32I, ecall, ebreak,
    fence, or a Zicsr instruction that reads cycle, cycleh, instret or instreth;
  * the recorded ISA string (Tag_RISCV_arch) has no M, F, D, A, C or V;
  * it has no undefined symbols (no libgcc, no libc).

Usage: rvcheck.py [--prefix riscv-none-elf-] FILE...
"""

import argparse
import os
import re
import subprocess
import sys

RV32I = set("""lui auipc jal jalr beq bne blt bge bltu bgeu lb lh lw lbu lhu sb sh sw
addi slti sltiu xori ori andi slli srli srai add sub sll slt sltu xor srl sra or
and fence ecall ebreak""".split())
CSR_OPS = {"csrrw", "csrrs", "csrrc", "csrrwi", "csrrsi", "csrrci"}
COUNTERS = {"cycle", "cycleh", "instret", "instreth"}
# Data words the disassembler shows in a code section: a table in .text, or an
# encoding it could not decode. Neither may hide a forbidden instruction.
RAW = re.compile(r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2,8}\s+)+(\.(?:word|4byte|2byte|short|byte|insn|dword|8byte)\b.*|unknown.*|\.\w+.*)$")
INSN = re.compile(r"^\s*[0-9a-f]+:\s+(?:(?:[0-9a-f]{8}|[0-9a-f]{4})\s+)+([a-z][a-z0-9._]*)\s*(.*)$")


def tool(prefix, name):
    return prefix + name


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=False)


# What an implementation object may refer to outside itself. The assembly is
# self-contained apart from the software multiply the runtime supplies; the C
# version may also call the string routines GCC emits.
ALLOWED_UNDEFINED = {
    "asm": {"__mulsi3"},
    "c": {"__mulsi3", "memcpy", "memmove", "memset", "memcmp", "strlen"},
}
# Instructions that belong to the harness: environment calls, breakpoints,
# fences and every CSR access. An implementation must not print, exit or touch
# the counters on its own.
HARNESS_ONLY = {"ecall", "ebreak", "fence"} | CSR_OPS


def check(path, prefix, impl=None):
    """impl is "asm" or "c" for an implementation object, None for a whole ELF."""
    problems = []
    dis = run([tool(prefix, "objdump"), "-d", "-M", "no-aliases", path])
    if dis.returncode != 0:
        return [f"objdump failed: {dis.stderr.strip()}"]
    seen = {}
    for line in dis.stdout.splitlines():
        m = INSN.match(line)
        if not m:
            raw = RAW.match(line)
            if raw:
                seen[f"data in a code section ({raw.group(1).split()[0]})"] = seen.get(f"data in a code section ({raw.group(1).split()[0]})", 0) + 1
            continue
        op, rest = m.groups()
        rest = rest.split("#")[0].strip()
        if impl and op in HARNESS_ONLY:
            seen[f"{op} (only the harness may use it)"] = seen.get(f"{op} (only the harness may use it)", 0) + 1
            continue
        if op in RV32I:
            continue
        if op in CSR_OPS:
            fields = [x.strip() for x in rest.split(",")]
            csr = fields[1] if len(fields) > 1 else ""
            src = fields[2] if len(fields) > 2 else ""
            # A read of a counter: csrrs or csrrc with rs1 = zero, or their
            # immediate forms with 0. csrrw and csrrwi always write, and the
            # counters are read-only.
            reads = op in ("csrrs", "csrrc") and src in ("zero", "x0")
            reads = reads or (op in ("csrrsi", "csrrci") and src in ("0", "0x0"))
            if csr in COUNTERS and reads:
                continue
            seen[f"{op} {csr}"] = seen.get(f"{op} {csr}", 0) + 1
            continue
        seen[op] = seen.get(op, 0) + 1
    for op, n in sorted(seen.items()):
        problems.append(f"forbidden instruction '{op}' x{n}")

    attr = run([tool(prefix, "readelf"), "-A", path]).stdout
    m = re.search(r'Tag_RISCV_arch: "([^"]*)"', attr)
    if not m:
        problems.append("no Tag_RISCV_arch attribute")
    else:
        parts = m.group(1).split("_")
        if not re.fullmatch(r"rv32i\d+p\d+", parts[0]):
            problems.append(f"ISA string '{m.group(1)}' is not plain rv32i")
        for ext in parts[1:]:
            if not re.match(r"(zicsr|zicntr)", ext):
                problems.append(f"extension '{ext}' in the ISA string")

    if path.endswith(".elf") or impl:
        undef = run([tool(prefix, "nm"), "-u", path]).stdout.split()
        # nm -u prints 'U name'; keep the names.
        names = [t for t in undef if t != "U" and t not in ALLOWED_UNDEFINED.get(impl, set())]
        if names:
            problems.append("undefined symbols: " + " ".join(names))
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prefix", default=os.environ.get("CROSS_COMPILE", "riscv-none-elf-"))
    ap.add_argument("--impl", choices=("asm", "c"), help="the files are implementation objects of that kind")
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()
    bad = 0
    for f in args.files:
        problems = check(f, args.prefix, args.impl)
        if problems:
            bad += 1
            print(f"{f}: FAIL")
            for p in problems:
                print(f"    {p}")
        else:
            print(f"{f}: ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

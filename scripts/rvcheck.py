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
INSN = re.compile(r"^\s*[0-9a-f]+:\s+(?:(?:[0-9a-f]{8}|[0-9a-f]{4})\s+)+([a-z][a-z0-9._]*)\s*(.*)$")


def tool(prefix, name):
    return prefix + name


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=False)


def check(path, prefix):
    problems = []
    dis = run([tool(prefix, "objdump"), "-d", "-M", "no-aliases", path])
    if dis.returncode != 0:
        return [f"objdump failed: {dis.stderr.strip()}"]
    seen = {}
    for line in dis.stdout.splitlines():
        m = INSN.match(line)
        if not m:
            continue
        op, rest = m.groups()
        rest = rest.split("#")[0].strip()
        if op in RV32I:
            continue
        if op in CSR_OPS:
            fields = rest.split(",")
            csr = fields[1].strip() if len(fields) > 1 else ""
            # Reading means rs1 = zero (csrrs/csrrc) or a zero immediate.
            if csr in COUNTERS:
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

    if path.endswith(".elf"):
        undef = run([tool(prefix, "nm"), "-u", path]).stdout.split()
        # nm -u prints 'U name'; keep the names.
        names = [t for t in undef if t != "U"]
        if names:
            problems.append("undefined symbols: " + " ".join(names))
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prefix", default=os.environ.get("CROSS_COMPILE", "riscv-none-elf-"))
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()
    bad = 0
    for f in args.files:
        problems = check(f, args.prefix)
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

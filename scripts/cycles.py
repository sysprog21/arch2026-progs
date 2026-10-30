#!/usr/bin/env python3
"""Measure, tabulate and plot the cycle counts of every program.

Reads the @@bench lines each ELF prints on rv32emu and writes under --out:

  cycles.csv     one row per function, implementation and optimization level
  cycles.md      tables: cycles per call, ratio to -O2, speedup of the assembly
  footprint.csv  size -A of each implementation, harness excluded
  *.png          bar charts (needs matplotlib; the tables are written either way)

rv32emu has no timing model. Its cycle counter counts executed guest
instructions, so these figures are instruction counts: they say nothing about
pipeline stalls, cache misses or multi-cycle instructions.
"""

import argparse
import csv
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from benchlib import BenchError, collect, sections  # noqa: E402

PROGS = ["rubik", "bf16", "gcd", "fixed"]
OPTS = ["O1", "O2", "O3", "Os"]
SECTIONS = [".text", ".rodata", ".data", ".bss"]
VARIANTS = ["-O1", "-O2", "-O3", "-Os", "asm"]


def sh(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=False).stdout


def per_call_table(rows):
    """{(prog, fn): {variant: cycles per call}}.

    The assembly is the same object in every ELF, but the caller around it is
    compiled at the ELF's optimization level, so its measured cost shifts by a
    fraction of an instruction. "asm" is the figure from the -O2 ELF; "asm@-O3"
    and the like are kept for the same-ELF comparisons in best_ratio().
    """
    t = {}
    for r in rows:
        cell = t.setdefault((r["prog"], r["fn"]), {})
        if r["impl"] == "asm":
            cell["asm@-" + r["opt"]] = r["per_call"]
            if r["opt"] == "O2":
                cell["asm"] = r["per_call"]
        else:
            cell["-" + r["opt"]] = r["per_call"]
    return t


def best_ratio(cell):
    """Cycles of the best C build over the assembly measured in the same ELF."""
    cs = [(cell[v], cell.get("asm@" + v)) for v in VARIANTS[:4] if v in cell]
    cs = [(c, a) for c, a in cs if a]
    if not cs:
        return None
    c, a = min(cs)
    return c / a


def footprint(build, prefix):
    out = []
    for prog in PROGS:
        objs = [("asm", f"{build}/{prog}/impl_asm.o")]
        objs += [("-" + o, f"{build}/{prog}/{o}/impl.o") for o in OPTS]
        for variant, path in objs:
            if os.path.exists(path):
                sec = sections(sh([prefix + "size", "-A", path]))
                out.append({"prog": prog, "variant": variant, **sec})
    return out


def fmt(x):
    return "-" if x is None else f"{x:.1f}"


def write_markdown(path, table, fp, meta):
    with open(path, "w") as f:
        f.write("# Cycle analysis\n\n")
        for k, v in meta.items():
            f.write(f"- {k}: {v}\n")
        f.write(
            "\nrv32emu has no timing model: `cycle` and `instret` are one counter of "
            "executed guest instructions. The figures below are instruction counts per "
            "call, with the loop control, input load and counter reads removed.\n\n"
        )
        f.write("## Cycles per call\n\n")
        f.write("| Function | -O1 | -O2 | -O3 | -Os | asm | best C | asm vs best C |\n|---|---|---|---|---|---|---|---|\n")
        for (prog, fn), c in sorted(table.items()):
            cs = [c.get(v) for v in VARIANTS[:4]]
            have = [x for x in cs if x is not None]
            best = min(have) if have else None
            asm = c.get("asm")
            r = best_ratio(c)
            ratio = f"{r:.2f}x" if r else "-"
            f.write(f"| {prog}.{fn} | " + " | ".join(fmt(x) for x in cs) + f" | {fmt(asm)} | {fmt(best)} | {ratio} |\n")
        f.write("\n## Ratio to -O2\n\n")
        f.write("| Function | -O1 | -O2 | -O3 | -Os | asm |\n|---|---|---|---|---|---|\n")
        for (prog, fn), c in sorted(table.items()):
            base = c.get("-O2")
            cells = [f"{c[v] / base:.2f}" if base and v in c else "-" for v in VARIANTS]
            f.write(f"| {prog}.{fn} | " + " | ".join(cells) + " |\n")
        f.write("\n## Footprint (bytes, `size -A`, harness excluded)\n\n")
        f.write("| Program | Variant | .text | .rodata | .data | .bss |\n|---|---|---|---|---|---|\n")
        for r in fp:
            f.write(f"| {r['prog']} | {r['variant']} | " + " | ".join(str(r[s]) for s in SECTIONS) + " |\n")


def plot(out, table, fp):
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib is not installed; tables written, charts skipped (pip install matplotlib)")
        return []
    written = []
    colors = {"-O1": "#9ecae1", "-O2": "#4292c6", "-O3": "#08519c", "-Os": "#6baed6", "asm": "#d94801"}
    keys = sorted(table)
    if keys:
        # 1. Cycles per call, every variant side by side (log scale: the functions
        #    differ by orders of magnitude, as in the per-operation charts of the
        #    fixed-point note).
        fig, ax = plt.subplots(figsize=(max(7, 1.2 * len(keys)), 4.2))
        w = 0.16
        for i, v in enumerate(VARIANTS):
            xs = [j + (i - 2) * w for j in range(len(keys))]
            ys = [table[k].get(v, 0) for k in keys]
            bars = ax.bar(xs, ys, w, label=v, color=colors[v])
            for b, y in zip(bars, ys):
                if y:
                    ax.text(b.get_x() + b.get_width() / 2, y, f"{y:.0f}", ha="center", va="bottom", fontsize=6)
        ax.set_xticks(range(len(keys)))
        ax.set_xticklabels([f"{p}.{f}" for p, f in keys], rotation=30, ha="right")
        ax.set_ylabel("instructions per call (rv32emu cycle counter)")
        ax.set_yscale("log")
        ax.set_title("Cycles per call: compiler builds against hand-written assembly")
        ax.legend(ncol=5, fontsize=8)
        fig.tight_layout()
        p = os.path.join(out, "cycles-per-call.png")
        fig.savefig(p, dpi=150)
        plt.close(fig)
        written.append(p)

        # 2. Speedup of the assembly over the best C build.
        fig, ax = plt.subplots(figsize=(max(6, 0.9 * len(keys)), 3.6))
        ratios = []
        for k in keys:
            ratios.append(best_ratio(table[k]) or 0)
        ax.bar(range(len(keys)), ratios, color=["#d94801" if r >= 1 else "#969696" for r in ratios])
        ax.axhline(1.0, color="black", linewidth=0.8)
        ax.set_xticks(range(len(keys)))
        ax.set_xticklabels([f"{p}.{f}" for p, f in keys], rotation=30, ha="right")
        ax.set_ylabel("best C cycles / asm cycles")
        ax.set_title("Speedup of the assembly (above 1 beats the best C build)")
        fig.tight_layout()
        p = os.path.join(out, "speedup.png")
        fig.savefig(p, dpi=150)
        plt.close(fig)
        written.append(p)

    if fp:
        # 3. Footprint per implementation, stacked by section.
        progs = [p for p in PROGS if any(r["prog"] == p for r in fp)]
        fig, ax = plt.subplots(figsize=(max(7, 1.6 * len(progs)), 4))
        w = 0.16
        sec_color = {".text": "#4292c6", ".rodata": "#fd8d3c", ".data": "#74c476", ".bss": "#bcbddc"}
        for i, v in enumerate(VARIANTS):
            for j, p in enumerate(progs):
                row = next((r for r in fp if r["prog"] == p and r["variant"] == v), None)
                if not row:
                    continue
                bottom = 0
                for s in SECTIONS:
                    ax.bar(j + (i - 2) * w, row[s], w, bottom=bottom, color=sec_color[s],
                           label=s if (i == 0 and j == 0) else None)
                    bottom += row[s]
                ax.text(j + (i - 2) * w, bottom, v.lstrip("-"), ha="center", va="bottom", fontsize=6)
        ax.set_xticks(range(len(progs)))
        ax.set_xticklabels(progs)
        ax.set_ylabel("bytes")
        ax.set_title("Footprint of each implementation (harness excluded)")
        ax.legend(fontsize=8)
        fig.tight_layout()
        p = os.path.join(out, "footprint.png")
        fig.savefig(p, dpi=150)
        plt.close(fig)
        written.append(p)
    return written


def report(rows, build, out, prefix):
    """Write the tables and charts for rows of benchmark data; return the file names."""
    os.makedirs(out, exist_ok=True)
    cols = ["prog", "fn", "impl", "opt", "n", "cycles", "overhead", "per_call", "instret", "per_call_instret", "checksum"]
    with open(os.path.join(out, "cycles.csv"), "w", newline="") as f:
        wr = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        wr.writeheader()
        wr.writerows(rows)
    fp = footprint(build, prefix)
    with open(os.path.join(out, "footprint.csv"), "w", newline="") as f:
        wr = csv.DictWriter(f, fieldnames=["prog", "variant"] + SECTIONS)
        wr.writeheader()
        wr.writerows(fp)

    table = per_call_table(rows)
    gcc = sh([prefix + "gcc", "-dumpfullversion"]).strip()
    rev = sh(["git", "-C", "third_party/rv32emu", "rev-parse", "--short", "HEAD"]).strip()
    meta = {
        "compiler": f"{prefix}gcc {gcc}, -march=rv32i_zicsr -mabi=ilp32 -ffreestanding",
        "rv32emu": f"{rev} (system emulation, ELF_LOADER, interpreter)",
        "timed region": "n calls on identical inputs; loop baseline subtracted",
    }
    write_markdown(os.path.join(out, "cycles.md"), table, fp, meta)
    written = plot(out, table, fp)
    return ["cycles.csv", "footprint.csv", "cycles.md"] + [os.path.basename(p) for p in written]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--emu", default="third_party/rv32emu/build/rv32emu")
    ap.add_argument("--build", default="build")
    ap.add_argument("--out", default="results")
    ap.add_argument("--prefix", default=os.environ.get("CROSS_COMPILE") or sh(["scripts/toolchain.sh", "--detect"]).strip())
    ap.add_argument("--timeout", type=int, default=120, help="seconds one ELF may run (default 120)")
    args = ap.parse_args()

    try:
        rows = collect(args.emu, args.build, PROGS, OPTS, args.timeout)
    except BenchError as e:
        print(e)
        return 1
    if not rows:
        print("no benchmark lines; build the ELF files first (make)")
        return 1
    names = report(rows, args.build, args.out, args.prefix)
    print(f"wrote {args.out}/" + ", ".join(names))
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Checks of the benchmark parser and the data accounting: make unittest."""

import unittest

from benchlib import BenchError, find_bench_lines, static_data

GOOD = (
    "@@bench prog=gcd fn=gcd impl=c opt=O2 n=16 cycles=1830 instret=1830 "
    "overhead=100 overhead_instret=100 checksum=28"
)


class BenchTest(unittest.TestCase):
    def test_well_formed_line(self):
        (row,) = find_bench_lines("noise\n" + GOOD + "\nmore noise")
        self.assertEqual((row["prog"], row["fn"], row["impl"], row["opt"]), ("gcd", "gcd", "c", "O2"))
        self.assertAlmostEqual(row["per_call"], (1830 - 100) / 16)

    def test_defects_are_errors_not_crashes(self):
        for bad in (
            GOOD.replace("n=16", "n=0"),  # no calls
            GOOD.replace("cycles=1830", "cycles=50"),  # below its own baseline
            GOOD.replace("checksum=28", "checksum=x"),  # not a number
            GOOD.replace(" fn=gcd", ""),  # a missing field
            GOOD.replace("n=16 ", ""),
        ):
            with self.subTest(bad=bad), self.assertRaises(BenchError):
                find_bench_lines(bad)


class StaticDataTest(unittest.TestCase):
    def test_every_allocated_section_counts_except_debug_and_notes(self):
        out = "\n".join(
            [
                "section size addr",
                ".text 100 0",
                ".text.hot 100 0",
                ".data 8 0",
                ".bss 16 0",
                ".sbss 4 0",
                ".rodata.cst8 8 0",
                ".lookup 1000 0",  # a table in a section of its own
                ".debug_info 999 0",
                ".comment 30 0",
                ".riscv.attributes 40 0",
                ".note.GNU-stack 0 0",
                "Total 5000",
            ]
        )
        # code counts too: a table in .text is still a table
        self.assertEqual(static_data(out), 100 + 100 + 8 + 16 + 4 + 8 + 1000)
        self.assertEqual(static_data(""), 0)


if __name__ == "__main__":
    unittest.main()

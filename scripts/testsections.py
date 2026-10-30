"""Regression checks for static-data accounting: python3 -m unittest discover -s scripts."""

import unittest
from types import SimpleNamespace
from unittest.mock import patch

from benchlib import sections
from driver import Result, check_data_budget


class SectionsTest(unittest.TestCase):
    def test_small_data_and_subsections(self):
        output = "\n".join(
            f"{name}{suffix} 8 0"
            for name in (".text", ".data", ".bss", ".rodata", ".sdata", ".sbss", ".srodata")
            for suffix in ("", ".foo")
        )
        output += "\n.sdatabase 999 0\n.srodatax.foo 999 0\n.debug_info 999 0"
        self.assertEqual(sections(output), {".text": 16, ".data": 32, ".bss": 32, ".rodata": 32})
        self.assertEqual(sections(""), dict.fromkeys((".text", ".data", ".bss", ".rodata"), 0))

    def test_budget_for_c_and_assembly(self):
        ctx = SimpleNamespace(build="build", prefix="riscv-none-elf-")
        for extra in (0, 1):
            with self.subTest(extra=extra), patch("driver.header_budget", return_value=131072), patch(
                "driver.os.path.exists", return_value=True
            ), patch(
                "driver.sh", return_value=SimpleNamespace(stdout=f".bss 131072 0\n.sdata.extra {extra} 0")
            ):
                result = Result("rubik")
                check_data_budget(ctx, "rubik", result)
                self.assertEqual(len(result.problems), 5 if extra else 0)
                if extra:
                    self.assertTrue(all("131073 bytes" in problem for problem in result.problems))


if __name__ == "__main__":
    unittest.main()

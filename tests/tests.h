#pragma once

/* Registrars of the tests shared by every program. */
void utest_register_tests(
    void); /* the assertion macros; also built for the host */
void rt_register_tests(
    void); /* runtime, printer, startup and counters; RISC-V only */

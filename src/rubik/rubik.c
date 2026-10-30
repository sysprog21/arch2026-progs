#include "rubik.h"

/* Dummy: reports every state as invalid. Replace with the optimal solver. */
int rubik_solve(const char *state, char *out, size_t cap)
{
    (void) state;
    (void) out;
    (void) cap;
    return -1;
}

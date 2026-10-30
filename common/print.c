#include "print.h"
#include <stdarg.h>
#include <stdint.h>
#include "rt.h"

void bm_write(const char *buf, size_t len)
{
    register long a0 __asm__("a0") = 1;
    register const char *a1 __asm__("a1") = buf;
    register size_t a2 __asm__("a2") = len;
    register long a7 __asm__("a7") = 64;
    /* The buffer is read by the emulator, so memory must be a clobber. */
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
}

static const uint64_t pow10_tab[] = {
    1ULL,
    10ULL,
    100ULL,
    1000ULL,
    10000ULL,
    100000ULL,
    1000000ULL,
    10000000ULL,
    100000000ULL,
    1000000000ULL,
    10000000000ULL,
    100000000000ULL,
    1000000000000ULL,
    10000000000000ULL,
    100000000000000ULL,
    1000000000000000ULL,
    10000000000000000ULL,
    100000000000000000ULL,
    1000000000000000000ULL,
    10000000000000000000ULL,
};

/* Repeated subtraction of powers of ten: no division, no multiply. */
static size_t fmt_u64(char *out, uint64_t v)
{
    size_t n = 0;
    int started = 0;
    for (int i = 19; i >= 0; i--) {
        char d = '0';
        while (v >= pow10_tab[i]) {
            v -= pow10_tab[i];
            d++;
        }
        if (d != '0' || started || i == 0) {
            out[n++] = d;
            started = 1;
        }
    }
    return n;
}

static size_t fmt_hex(char *out, uint64_t v)
{
    size_t n = 0;
    int shift = 60;
    while (shift > 0 && !((v >> shift) & 0xf))
        shift -= 4;
    for (; shift >= 0; shift -= 4)
        out[n++] = "0123456789abcdef"[(v >> shift) & 0xf];
    return n;
}

typedef void (*sink_fn)(void *ctx, const char *s, size_t n);

/* Format into a sink; return the number of characters produced. */
static int format(sink_fn sink, void *ctx, const char *fmt, va_list ap)
{
    int total = 0;
    char num[24];
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            sink(ctx, fmt, 1);
            total++;
            continue;
        }
        fmt++;
        int lng = 0;
        while (*fmt == 'l') {
            lng++;
            fmt++;
        }
        const char *s = num;
        size_t n = 0;
        switch (*fmt) {
        case 's':
            s = va_arg(ap, const char *);
            n = strlen(s);
            break;
        case 'c':
            num[0] = (char) va_arg(ap, int);
            n = 1;
            break;
        case 'd': {
            int64_t v = lng >= 2 ? va_arg(ap, long long) : va_arg(ap, int);
            uint64_t u = (uint64_t) v;
            if (v < 0) {
                num[0] = '-';
                n = 1;
                u = 0 - u;
            }
            n += fmt_u64(num + n, u);
            break;
        }
        case 'u':
            n = fmt_u64(num, lng >= 2 ? va_arg(ap, unsigned long long)
                                      : va_arg(ap, unsigned int));
            break;
        case 'x':
            n = fmt_hex(num, lng >= 2 ? va_arg(ap, unsigned long long)
                                      : va_arg(ap, unsigned int));
            break;
        case 'p':
            num[0] = '0';
            num[1] = 'x';
            n = 2 + fmt_hex(num + 2, (uintptr_t) va_arg(ap, void *));
            break;
        case '%':
            num[0] = '%';
            n = 1;
            break;
        default:
            continue;
        }
        sink(ctx, s, n);
        total += (int) n;
    }
    return total;
}

static void sink_stdout(void *ctx, const char *s, size_t n)
{
    (void) ctx;
    bm_write(s, n);
}

struct buf {
    char *p;
    size_t cap, len;
};

static void sink_buf(void *ctx, const char *s, size_t n)
{
    struct buf *b = ctx;
    for (size_t i = 0; i < n; i++, b->len++) {
        if (b->len + 1 < b->cap)
            b->p[b->len] = s[i];
    }
}

int bm_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = format(sink_stdout, 0, fmt, ap);
    va_end(ap);
    return r;
}

int bm_snprintf(char *out, size_t cap, const char *fmt, ...)
{
    struct buf b = {out, cap, 0};
    va_list ap;
    va_start(ap, fmt);
    int r = format(sink_buf, &b, fmt, ap);
    va_end(ap);
    if (cap)
        out[b.len < cap ? b.len : cap - 1] = '\0';
    return r;
}

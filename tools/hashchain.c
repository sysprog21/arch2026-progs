/*
 * hashchain: the hash chain behind the Change-Id of every commit.
 *
 * Each commit after the base commit carries a Change-Id that links it to its
 * parent:
 *
 *   seed      = H("arch2026-progs/" base-commit-id)
 *   change_id = H(previous change_id ":" tree-id)    (first 40 hex digits)
 *
 * The commit-msg hook computes the link when a commit is made; `make` and the
 * GitHub Actions workflow recompute the whole chain from `git log`. A commit
 * made with --no-verify has no valid link, and neither has one whose tree or
 * ancestry was altered afterwards. The hash is SHA-256 written out below, so no
 * library is needed. The chain detects skipped hooks and edited history; it is
 * not a defence against someone who runs this tool on purpose.
 *
 *   hashchain selftest              check the SHA-256 implementation
 *   hashchain seed BASE             print the seed of a chain
 *   hashchain next PREVIOUS TREE    print the Change-Id digits that follow
 *   hashchain verify BASE [COUNT]   read "TREE CHANGE-ID" lines, oldest first,
 *                                   from stdin and check the whole chain;
 *                                   COUNT, when given, is the line count
 */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ID_DIGITS 40

struct sha256 {
    uint32_t h[8];
    uint8_t block[64];
    uint64_t total;
    size_t fill;
};

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t ror(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

static void compress(struct sha256 *s, const uint8_t *p)
{
    uint32_t w[64], v[8];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t) p[4 * i] << 24 | (uint32_t) p[4 * i + 1] << 16 |
               (uint32_t) p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, s->h, sizeof v);
    for (int i = 0; i < 64; i++) {
        uint32_t s1 = ror(v[4], 6) ^ ror(v[4], 11) ^ ror(v[4], 25);
        uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        uint32_t t1 = v[7] + s1 + ch + K[i] + w[i];
        uint32_t s0 = ror(v[0], 2) ^ ror(v[0], 13) ^ ror(v[0], 22);
        uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        uint32_t t2 = s0 + maj;
        memmove(v + 1, v, 7 * sizeof v[0]);
        v[4] += t1;
        v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++)
        s->h[i] += v[i];
}

static void sha256_init(struct sha256 *s)
{
    static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                   0xa54ff53a, 0x510e527f, 0x9b05688c,
                                   0x1f83d9ab, 0x5be0cd19};
    memcpy(s->h, iv, sizeof iv);
    s->total = 0;
    s->fill = 0;
}

static void sha256_update(struct sha256 *s, const void *data, size_t len)
{
    const uint8_t *p = data;
    s->total += len;
    while (len) {
        size_t n = 64 - s->fill < len ? 64 - s->fill : len;
        memcpy(s->block + s->fill, p, n);
        s->fill += n;
        p += n;
        len -= n;
        if (s->fill == 64) {
            compress(s, s->block);
            s->fill = 0;
        }
    }
}

static void sha256_final(struct sha256 *s, uint8_t out[32])
{
    uint64_t bits = s->total * 8;
    uint8_t pad = 0x80;
    sha256_update(s, &pad, 1);
    pad = 0;
    while (s->fill != 56)
        sha256_update(s, &pad, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++)
        len[i] = (uint8_t) (bits >> (56 - 8 * i));
    sha256_update(s, len, 8);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t) (s->h[i] >> 24);
        out[4 * i + 1] = (uint8_t) (s->h[i] >> 16);
        out[4 * i + 2] = (uint8_t) (s->h[i] >> 8);
        out[4 * i + 3] = (uint8_t) s->h[i];
    }
}

static void to_hex(const uint8_t *d, size_t n, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = digits[d[i] >> 4];
        out[2 * i + 1] = digits[d[i] & 15];
    }
    out[2 * n] = '\0';
}

/* hex(SHA-256(a || b)), cut to ID_DIGITS characters. */
static void digest(const char *a, const char *b, char out[ID_DIGITS + 1])
{
    struct sha256 s;
    uint8_t d[32];
    char hex[65];
    sha256_init(&s);
    sha256_update(&s, a, strlen(a));
    sha256_update(&s, b, strlen(b));
    sha256_final(&s, d);
    to_hex(d, 32, hex);
    memcpy(out, hex, ID_DIGITS);
    out[ID_DIGITS] = '\0';
}

static void seed(const char *base, char out[ID_DIGITS + 1])
{
    digest("arch2026-progs/", base, out);
}

static void next(const char *prev, const char *tree, char out[ID_DIGITS + 1])
{
    char joined[128];
    snprintf(joined, sizeof joined, "%s:%s", prev, tree);
    digest("", joined, out);
}

static int selftest(void)
{
    static const struct {
        const char *msg, *hex;
    } vec[] = {
        {"",
         "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc",
         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
    };
    for (size_t i = 0; i < sizeof vec / sizeof vec[0]; i++) {
        struct sha256 s;
        uint8_t d[32];
        char hex[65];
        sha256_init(&s);
        sha256_update(&s, vec[i].msg, strlen(vec[i].msg));
        sha256_final(&s, d);
        to_hex(d, 32, hex);
        if (strcmp(hex, vec[i].hex)) {
            fprintf(stderr, "SHA-256 self-test %zu failed: %s\n", i, hex);
            return 1;
        }
    }
    return 0;
}

static int is_hex(const char *s, size_t n)
{
    if (strlen(s) != n)
        return 0;
    for (; *s; s++) {
        if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f')))
            return 0;
    }
    return 1;
}

/* Check a chain read from stdin: one "TREE CHANGE-ID" line per commit, oldest
 * first. Each line must hold exactly those two tokens. When expected is not
 * negative the number of lines must equal it.
 */
static int verify(const char *base, long expected)
{
    char prev[ID_DIGITS + 1], want[ID_DIGITS + 1];
    char line[256], tree[128], id[128];
    long n = 0;
    seed(base, prev);
    while (fgets(line, sizeof line, stdin)) {
        int used = 0;
        n++;
        if (!strchr(line, '\n') && !feof(stdin)) {
            fprintf(stderr, "commit %ld: input line too long\n", n);
            return 1;
        }
        if (sscanf(line, "%127s %127s%n", tree, id, &used) != 2 ||
            !is_hex(tree, 40) || id[0] != 'I' || !is_hex(id + 1, ID_DIGITS)) {
            fprintf(stderr, "commit %ld: missing or malformed Change-Id\n", n);
            return 1;
        }
        for (const char *p = line + used; *p; p++) {
            if (!isspace((unsigned char) *p)) {
                fprintf(stderr, "commit %ld: text after the Change-Id\n", n);
                return 1;
            }
        }
        next(prev, tree, want);
        if (strcmp(id + 1, want)) {
            fprintf(stderr,
                    "commit %ld: Change-Id does not continue the chain\n", n);
            return 1;
        }
        memcpy(prev, want, sizeof prev);
    }
    if (ferror(stdin)) {
        fprintf(stderr, "cannot read the chain\n");
        return 1;
    }
    if (expected >= 0 && n != expected) {
        fprintf(stderr, "commit %ld: expected %ld commits, read %ld\n", n + 1,
                expected, n);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    char out[ID_DIGITS + 1];
    if (argc == 2 && !strcmp(argv[1], "selftest"))
        return selftest();
    if (argc == 3 && !strcmp(argv[1], "seed") && is_hex(argv[2], 40)) {
        seed(argv[2], out);
        puts(out);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "next") && is_hex(argv[2], ID_DIGITS) &&
        is_hex(argv[3], 40)) {
        next(argv[2], argv[3], out);
        puts(out);
        return 0;
    }
    if ((argc == 3 || argc == 4) && !strcmp(argv[1], "verify") &&
        is_hex(argv[2], 40)) {
        long expected = -1;
        if (argc == 4 && sscanf(argv[3], "%ld", &expected) != 1)
            expected = -1;
        return verify(argv[2], expected);
    }
    fprintf(stderr,
            "usage: hashchain selftest | seed BASE | next PREVIOUS TREE | "
            "verify BASE [COUNT]\n");
    return 2;
}

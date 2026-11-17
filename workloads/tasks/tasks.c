// Task kernels. See tasks.h for the contract.
//
// Sizes are chosen so each task fits its 256 KiB scratch buffer, and so that
// one rep takes very roughly 100k-400k cycles on Rocket. Tune `reps` in
// workloads/mixes.json after the single-core runs, not the sizes here.

#include "tasks.h"

static inline uint32_t xs32(uint32_t *s) {      // xorshift32
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

static inline uint64_t mix64(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

static inline uint64_t dbits(double d) {
    union { double d; uint64_t u; } c; c.d = d; return c.u;
}

// ------------------------------------------------------------ branch-heavy

// Quicksort of random 32-bit keys: data-dependent compares, unpredictable.
#define QS_N 2048
static void qs(int32_t *a, int lo, int hi) {
    while (lo < hi) {
        int32_t p = a[(lo + hi) >> 1];
        int i = lo, j = hi;
        while (i <= j) {
            while (a[i] < p) i++;
            while (a[j] > p) j--;
            if (i <= j) { int32_t t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
        }
        if (j - lo < hi - i) { qs(a, lo, j); lo = i; }
        else                 { qs(a, i, hi); hi = j; }
    }
}
static uint64_t task_qsort(uint8_t *mem, uint32_t seed, int reps) {
    int32_t *a = (int32_t *)mem;
    uint64_t h = 0;
    for (int r = 0; r < reps; r++) {
        uint32_t s = seed + r * 7919u + 1;
        for (int i = 0; i < QS_N; i++) a[i] = (int32_t)xs32(&s);
        qs(a, 0, QS_N - 1);
        for (int i = 0; i < QS_N; i += 64) h = mix64(h, (uint32_t)a[i]);
    }
    return h;
}

// Random two-way branches with a little work on each side (~50% mispredict).
#define BR_ITERS 20000
static uint64_t task_brrand(uint8_t *mem, uint32_t seed, int reps) {
    (void)mem;
    uint64_t acc = 0;
    uint32_t s = seed | 1;
    for (int r = 0; r < reps; r++) {
        for (int i = 0; i < BR_ITERS; i++) {
            uint32_t x = xs32(&s);
            if (x & 0x100) acc += x >> 3;
            else           acc ^= (uint64_t)x << 7;
            if ((x & 0x3000) == 0x1000) acc = acc * 3 + 1;
        }
    }
    return acc;
}

// 3-point median filter over random data: three data-dependent compares.
#define MED_N 8192
static inline int32_t med3(int32_t a, int32_t b, int32_t c) {
    if (a > b) { if (b > c) return b; else if (a > c) return c; else return a; }
    else       { if (a > c) return a; else if (b > c) return c; else return b; }
}
static uint64_t task_median(uint8_t *mem, uint32_t seed, int reps) {
    int32_t *in = (int32_t *)mem, *out = in + MED_N;
    uint64_t h = 0;
    for (int r = 0; r < reps; r++) {
        uint32_t s = seed + r * 104729u + 3;
        for (int i = 0; i < MED_N; i++) in[i] = (int32_t)(xs32(&s) & 0xffff);
        out[0] = in[0]; out[MED_N - 1] = in[MED_N - 1];
        for (int i = 1; i < MED_N - 1; i++) out[i] = med3(in[i - 1], in[i], in[i + 1]);
        for (int i = 0; i < MED_N; i += 32) h = mix64(h, (uint32_t)out[i]);
    }
    return h;
}

// ------------------------------------------------------------ memory-heavy

// Pointer chase: 4096 nodes, one per 64-byte line (256 KiB), visited in a
// long-stride cycle. Every load depends on the previous one and misses L1.
#define PC_NODES  4096
#define PC_STRIDE 1031              // odd, so the walk visits every node
#define PC_WORDS_PER_NODE 8         // 8 x 8 B = one 64-byte line
static uint64_t task_ptrchase(uint8_t *mem, uint32_t seed, int reps) {
    uint64_t *n = (uint64_t *)mem;
    for (int i = 0; i < PC_NODES; i++)
        n[i * PC_WORDS_PER_NODE] = (uint64_t)((i + PC_STRIDE) % PC_NODES);
    uint64_t p = seed % PC_NODES, h = 0;
    for (int r = 0; r < reps; r++)
        for (int k = 0; k < PC_NODES * 4; k++) {
            p = n[p * PC_WORDS_PER_NODE];
            h = h * 31 + p;                 // order-sensitive, off the load chain
        }
    return h;
}

// STREAM triad a = b + s*c over three 64 KiB arrays (192 KiB > L1).
#define ST_N 8192
static uint64_t task_stream(uint8_t *mem, uint32_t seed, int reps) {
    double *a = (double *)mem, *b = a + ST_N, *c = b + ST_N;
    double sc = 1.0 + (seed & 7);
    for (int i = 0; i < ST_N; i++) { b[i] = i * 0.5; c[i] = (ST_N - i) * 0.25; }
    for (int r = 0; r < reps; r++)
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < ST_N; i++) a[i] = b[i] + sc * c[i];
    double sum = 0;
    for (int i = 0; i < ST_N; i += 16) sum += a[i];
    return dbits(sum);
}

// Sparse matrix-vector multiply (CSR, 8 random columns per row).
// Indirect loads x[col[k]] over a 64 KiB vector.
#define SP_ROWS 1536
#define SP_NNZ_ROW 8
#define SP_COLS 8192
static uint64_t task_spmv(uint8_t *mem, uint32_t seed, int reps) {
    double  *x   = (double *)mem;                       // 64 KiB
    double  *val = x + SP_COLS;                         // 96 KiB
    int32_t *col = (int32_t *)(val + SP_ROWS * SP_NNZ_ROW);   // 48 KiB
    double  *y   = (double *)(col + SP_ROWS * SP_NNZ_ROW);    // 12 KiB
    uint32_t s = seed | 1;
    for (int i = 0; i < SP_COLS; i++) x[i] = (double)(i & 255) * 0.125;
    for (int k = 0; k < SP_ROWS * SP_NNZ_ROW; k++) {
        col[k] = (int32_t)(xs32(&s) % SP_COLS);
        val[k] = (double)(k & 15) + 0.5;
    }
    for (int r = 0; r < reps; r++)
        for (int i = 0; i < SP_ROWS; i++) {
            double acc = 0;
            for (int k = i * SP_NNZ_ROW; k < (i + 1) * SP_NNZ_ROW; k++)
                acc += val[k] * x[col[k]];
            y[i] = acc;
        }
    double sum = 0;
    for (int i = 0; i < SP_ROWS; i++) sum += y[i];
    return dbits(sum);
}

// ------------------------------------------------------------ compute-heavy

// 32x32 integer matrix multiply: fits in L1, regular loops, lots of ILP.
#define MM_N 32
static uint64_t task_matmul(uint8_t *mem, uint32_t seed, int reps) {
    int32_t *A = (int32_t *)mem, *B = A + MM_N * MM_N, *C = B + MM_N * MM_N;
    uint32_t s = seed | 1;
    for (int i = 0; i < MM_N * MM_N; i++) { A[i] = xs32(&s) & 255; B[i] = xs32(&s) & 255; }
    uint64_t h = 0;
    for (int r = 0; r < reps; r++) {
        for (int i = 0; i < MM_N; i++)
            for (int j = 0; j < MM_N; j++) {
                int32_t acc = 0;
                for (int k = 0; k < MM_N; k++) acc += A[i * MM_N + k] * B[k * MM_N + j];
                C[i * MM_N + j] = acc;
            }
        h = mix64(h, (uint32_t)C[(r * 37) % (MM_N * MM_N)]);
        A[r % (MM_N * MM_N)] += 1;            // keep reps from being identical
    }
    return h;
}

// Table-driven CRC-32 over 32 KiB of data: predictable loop, table lookups.
#define CRC_BYTES (32 * 1024)
static uint64_t task_crc32(uint8_t *mem, uint32_t seed, int reps) {
    uint32_t *tab = (uint32_t *)mem;
    uint8_t *data = mem + 1024;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        tab[i] = c;
    }
    uint32_t s = seed | 1;
    for (int i = 0; i < CRC_BYTES; i += 4) {
        uint32_t v = xs32(&s);
        data[i] = v; data[i + 1] = v >> 8; data[i + 2] = v >> 16; data[i + 3] = v >> 24;
    }
    uint64_t h = 0;
    for (int r = 0; r < reps; r++) {
        uint32_t crc = 0xFFFFFFFFu ^ (uint32_t)r;
        for (int i = 0; i < CRC_BYTES; i++) crc = tab[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
        h = mix64(h, crc ^ 0xFFFFFFFFu);
    }
    return h;
}

// 16-tap FIR filter in double precision: FP multiply-add chains.
#define FIR_N 4096
#define FIR_TAPS 16
static uint64_t task_fir(uint8_t *mem, uint32_t seed, int reps) {
    double *in = (double *)mem, *out = in + FIR_N + FIR_TAPS, *coef = out + FIR_N;
    for (int i = 0; i < FIR_TAPS; i++) coef[i] = 1.0 / (i + 2);
    for (int i = 0; i < FIR_N + FIR_TAPS; i++) in[i] = (double)((i * 7 + (int)(seed & 31)) & 63) - 31.5;
    for (int r = 0; r < reps; r++)
        for (int i = 0; i < FIR_N; i++) {
            double acc = 0;
            for (int k = 0; k < FIR_TAPS; k++) acc += coef[k] * in[i + k];
            out[i] = acc;
        }
    double sum = 0;
    for (int i = 0; i < FIR_N; i += 8) sum += out[i];
    return dbits(sum);
}

const task_desc_t task_table[T_COUNT] = {
    [T_QSORT]    = { "qsort",    "branch",  task_qsort },
    [T_BRRAND]   = { "brrand",   "branch",  task_brrand },
    [T_MEDIAN]   = { "median",   "branch",  task_median },
    [T_PTRCHASE] = { "ptrchase", "memory",  task_ptrchase },
    [T_STREAM]   = { "stream",   "memory",  task_stream },
    [T_SPMV]     = { "spmv",     "memory",  task_spmv },
    [T_MATMUL]   = { "matmul",   "compute", task_matmul },
    [T_CRC32]    = { "crc32",    "compute", task_crc32 },
    [T_FIR]      = { "fir",      "compute", task_fir },
};

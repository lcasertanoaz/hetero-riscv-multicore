// Task kernels for the heterogeneous multicore study.
// Each task works only in its own scratch buffer (one per hart), builds its
// input from `seed`, repeats its work `reps` times, and returns a checksum.
// The same task with the same seed and reps must give the same checksum on
// every core type; parse_logs.py checks this.
#ifndef TASKS_H
#define TASKS_H

#include <stdint.h>

#define SCRATCH_BYTES (256 * 1024)

typedef uint64_t (*task_fn_t)(uint8_t *scratch, uint32_t seed, int reps);

typedef struct {
    const char *name;
    const char *kind;     // "branch", "memory" or "compute"
    task_fn_t fn;
} task_desc_t;

// Task ids: keep in the same order as task_table in tasks.c and TASKS in
// scripts/gen_workloads.py.
enum {
    T_QSORT, T_BRRAND, T_MEDIAN,      // branch-heavy
    T_PTRCHASE, T_STREAM, T_SPMV,     // memory-heavy
    T_MATMUL, T_CRC32, T_FIR,         // compute-heavy
    T_COUNT
};

extern const task_desc_t task_table[T_COUNT];

#endif

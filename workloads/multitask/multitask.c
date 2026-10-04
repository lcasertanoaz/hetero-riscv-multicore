// Static task assignment for a heterogeneous multicore run.
//
// Each hart runs the task(s) assigned to it, records its own cycle count, and
// meets the others at a barrier; hart 0 then prints the results.
//
// Written against the riscv-tests benchmarks environment (common/crt.S,
// syscalls.c, util.h), where every hart calls thread_entry(cid, nc) before
// hart 0 continues to main(). Check which hart IDs are BOOM vs. Rocket in the
// generated device tree for each config before filling in the assignment.
//
// STATUS: starter skeleton, not yet built.

#include <stdint.h>
#include <stdio.h>
#include "util.h"

#define MAX_HARTS 8

// Task bodies come from riscv-tests / Embench-IoT sources linked into this program.
extern void task_dhrystone(void);
extern void task_qsort(void);
extern void task_vvadd(void);
extern void task_spmv(void);

typedef void (*task_fn)(void);

// assignment[hart] = list of tasks for that hart, NULL-terminated.
// TODO: generate this per config from scripts/predict.py.
static task_fn assignment[MAX_HARTS][4] = {
    {task_dhrystone, task_qsort, 0},   // hart 0
    {task_vvadd, 0},                   // hart 1
    {task_spmv, 0},                    // hart 2
};

static volatile uint64_t hart_cycles[MAX_HARTS];

static inline uint64_t read_cycles(void) {
    uint64_t c;
    __asm__ volatile ("rdcycle %0" : "=r"(c));
    return c;
}

void thread_entry(int cid, int nc) {
    barrier(nc);                                   // start together
    uint64_t start = read_cycles();
    for (int t = 0; t < 4 && assignment[cid][t]; t++)
        assignment[cid][t]();
    hart_cycles[cid] = read_cycles() - start;
    barrier(nc);                                   // wait for every hart

    if (cid == 0) {
        uint64_t makespan = 0;
        for (int h = 0; h < nc; h++) {
            printf("hart %d: %lu cycles\n", h, (unsigned long)hart_cycles[h]);
            if (hart_cycles[h] > makespan) makespan = hart_cycles[h];
        }
        printf("workload completion: %lu cycles\n", (unsigned long)makespan);
        exit(0);
    }
    while (1) ;                                    // other harts park here
}

int main(void) { return 0; }                       // not reached: hart 0 exits above

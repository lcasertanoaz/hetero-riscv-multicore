// Small runtime helpers for the bare-metal multicore harness:
// CSR access, core-type detection, performance counters, barrier.
#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdint.h>

#define MAX_HARTS 8

#define read_csr(reg) ({ unsigned long __v; \
    __asm__ volatile ("csrr %0, " #reg : "=r"(__v)); __v; })
#define write_csr(reg, val) __asm__ volatile ("csrw " #reg ", %0" :: "r"(val))

// marchid values
#define ARCH_ROCKET 1
#define ARCH_BOOM   2

// Event codes, verified on BudgetR1/SB1/MB1Config (Nov 15, 2026).
// code = (event set) | 1 << (8 + position in set)
//   Rocket: rocket-chip RocketCore.scala EventSets; BOOM v3: exu/core.scala.
// BOOM's "branch resolved" event reads 0 in this Chipyard, so branch counts
// are not collected; the committed branch count is the same on every core.
#define EV_ROCKET_BR_MISPRED 0x2001UL
#define EV_BOOM_BR_MISPRED   0x0201UL
#define EV_ICACHE_MISS       0x0102UL
#define EV_DCACHE_MISS       0x0202UL

static inline const char *arch_name(unsigned long a) {
    return a == ARCH_BOOM ? "boom" : a == ARCH_ROCKET ? "rocket" : "unknown";
}

// mhpmcounter3 = branch mispredicts, 4 = I$ misses, 5 = D$ misses
static inline void counters_setup(unsigned long arch) {
    write_csr(mhpmevent3, arch == ARCH_BOOM ? EV_BOOM_BR_MISPRED : EV_ROCKET_BR_MISPRED);
    write_csr(mhpmevent4, EV_ICACHE_MISS);
    write_csr(mhpmevent5, EV_DCACHE_MISS);
}

typedef struct {
    uint64_t cycle, instret, brmiss, imiss, dmiss;
} snap_t;

static inline void snap(snap_t *s) {
    s->cycle   = read_csr(mcycle);
    s->instret = read_csr(minstret);
    s->brmiss  = read_csr(mhpmcounter3);
    s->imiss   = read_csr(mhpmcounter4);
    s->dmiss   = read_csr(mhpmcounter5);
}

// Sense-reversing barrier (same algorithm as riscv-tests util.h).
static volatile int barrier_count;
static volatile int barrier_sense;
static __thread int barrier_local_sense;

static void __attribute__((noinline)) barrier(int n) {
    __sync_synchronize();
    barrier_local_sense = !barrier_local_sense;
    if (__sync_fetch_and_add(&barrier_count, 1) == n - 1) {
        barrier_count = 0;
        barrier_sense = barrier_local_sense;
    } else {
        while (barrier_sense != barrier_local_sense)
            ;
    }
    __sync_synchronize();
}

#endif

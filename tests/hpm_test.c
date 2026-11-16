// Performance-counter smoke test (single core, bare metal).
//
// Checks that mcycle/minstret and the programmable counters mhpmcounter3..6
// work, using Rocket's event codes. Run it on BudgetR1Config first.
//
// Build (Chipyard toolchain on PATH, i.e. after `source env.sh`):
//   riscv64-unknown-elf-gcc -O2 -mcmodel=medany -specs=htif_nano.specs -o hpm_test.riscv tests/hpm_test.c
// Check it on Spike first (counters read 0 there, that's expected):
//   spike hpm_test.riscv
// Run on the RTL:
//   cd $CHIPYARD_DIR/sims/verilator
//   make CONFIG=BudgetR1Config run-binary LOADMEM=1 BINARY=<path>/hpm_test.riscv
//
// GOOD: non-zero numbers for all four events and "hpm_test done".
// BAD:  an illegal-instruction trap or a hang at the first csrw -> counters
//       are not enabled (WithHeteroPerfCounters missing or not leftmost).
//
// Rocket event codes: (event set in bits 7:0) | 1 << (8 + index in that set).
// Sets are listed in rocket-chip RocketCore.scala ("EventSets"); verify the
// indices there. BOOM uses different codes (BOOM core.scala).

#include <stdio.h>
#include <stdint.h>

#define read_csr(reg) ({ unsigned long __v; \
    __asm__ volatile ("csrr %0, " #reg : "=r"(__v)); __v; })
#define write_csr(reg, val) __asm__ volatile ("csrw " #reg ", %0" :: "r"(val))

#define EV_BRANCH      0x4000UL  // set 0, index 6
#define EV_BR_MISPRED  0x2001UL  // set 1, index 5
#define EV_ICACHE_MISS 0x0102UL  // set 2, index 0
#define EV_DCACHE_MISS 0x0202UL  // set 2, index 1

#define ARRAY_WORDS (64 * 1024 / 8)   // 64 KiB: bigger than any L1 D-cache here
static volatile uint64_t data[ARRAY_WORDS];

int main(void) {
    write_csr(mhpmevent3, EV_BRANCH);
    write_csr(mhpmevent4, EV_BR_MISPRED);
    write_csr(mhpmevent5, EV_ICACHE_MISS);
    write_csr(mhpmevent6, EV_DCACHE_MISS);

    unsigned long c0 = read_csr(mcycle), i0 = read_csr(minstret);
    unsigned long h3 = read_csr(mhpmcounter3), h4 = read_csr(mhpmcounter4);
    unsigned long h5 = read_csr(mhpmcounter5), h6 = read_csr(mhpmcounter6);

    // Unpredictable branches: pseudo-random bit decides the path.
    uint32_t x = 12345, acc = 0;
    for (int i = 0; i < 20000; i++) {
        x = x * 1103515245u + 12345u;
        if (x & 0x10000) acc += i; else acc ^= i;
    }
    // D-cache misses: touch one word per 64-byte line across 64 KiB, twice.
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < ARRAY_WORDS; i += 8)
            data[i] += acc + pass;

    unsigned long cyc = read_csr(mcycle) - c0, ins = read_csr(minstret) - i0;
    printf("cycles        %lu\n", cyc);
    printf("instret       %lu\n", ins);
    printf("branches      %lu\n", read_csr(mhpmcounter3) - h3);
    printf("br_mispred    %lu\n", read_csr(mhpmcounter4) - h4);
    printf("icache_miss   %lu\n", read_csr(mhpmcounter5) - h5);
    printf("dcache_miss   %lu\n", read_csr(mhpmcounter6) - h6);
    printf("checksum      %lu\n", (unsigned long)(acc + data[8]));
    printf("hpm_test done\n");
    return 0;
}

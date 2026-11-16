// Performance-counter smoke test (single core, bare metal).
//
// Reads marchid to tell Rocket (1) from BOOM (2), programs mhpmevent3..6 with
// that core's event codes, and prints cycles, instructions and four counters.
//
// Build (inside the cy container):
//   riscv64-unknown-elf-gcc -O2 -mcmodel=medany -specs=htif_nano.specs -o hpm_test.riscv tests/hpm_test.c
// Run:
//   cd /workspace/chipyard/sims/verilator
//   make CONFIG=BudgetR1Config run-binary-fast BINARY=<path>/hpm_test.riscv
//   cat output/*BudgetR1Config*/hpm_test.log
//
// Event code = (event set) | 1 << (8 + position in that set).
// Rocket: rocket-chip RocketCore.scala (EventSets). BOOM v3: boom exu/core.scala.
//
//              Rocket                      BOOM v3
// branches     0x4000 set0 pos6 (branch)   0x1001 set1 pos4 (branch resolved)
// br_mispred   0x2001 set1 pos5            0x0201 set1 pos1
// icache_miss  0x0102 set2 pos0            0x0102 set2 pos0
// dcache_miss  0x0202 set2 pos1            0x0202 set2 pos1
//
// Note: BOOM counts branches when they RESOLVE in the branch unit (this can
// include wrong-path branches); Rocket counts committed conditional branches.
// Compare mispredicts per 1000 instructions, not raw branch counts.

#include <stdio.h>
#include <stdint.h>

#define read_csr(reg) ({ unsigned long __v; \
    __asm__ volatile ("csrr %0, " #reg : "=r"(__v)); __v; })
#define write_csr(reg, val) __asm__ volatile ("csrw " #reg ", %0" :: "r"(val))

#define MARCHID_ROCKET 1
#define MARCHID_BOOM   2

#define ARRAY_WORDS (64 * 1024 / 8)   // 64 KiB: bigger than any L1 D-cache here
static volatile uint64_t data[ARRAY_WORDS];

int main(void) {
    unsigned long arch = read_csr(marchid);
    const char *core = arch == MARCHID_BOOM ? "boom" : arch == MARCHID_ROCKET ? "rocket" : "unknown";

    unsigned long ev_branch  = arch == MARCHID_BOOM ? 0x1001UL : 0x4000UL;
    unsigned long ev_mispred = arch == MARCHID_BOOM ? 0x0201UL : 0x2001UL;
    write_csr(mhpmevent3, ev_branch);
    write_csr(mhpmevent4, ev_mispred);
    write_csr(mhpmevent5, 0x0102UL);   // I$ miss (same on both)
    write_csr(mhpmevent6, 0x0202UL);   // D$ miss (same on both)

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
    unsigned long br = read_csr(mhpmcounter3) - h3, mp = read_csr(mhpmcounter4) - h4;
    unsigned long im = read_csr(mhpmcounter5) - h5, dm = read_csr(mhpmcounter6) - h6;

    printf("core          %s (marchid %lu)\n", core, arch);
    printf("cycles        %lu\n", cyc);
    printf("instret       %lu\n", ins);
    printf("ipc_x1000     %lu\n", ins * 1000 / cyc);
    printf("branches      %lu\n", br);
    printf("br_mispred    %lu\n", mp);
    printf("icache_miss   %lu\n", im);
    printf("dcache_miss   %lu\n", dm);
    printf("br_mpki_x10   %lu\n", mp * 10000 / ins);
    printf("dmiss_mpki_x10 %lu\n", dm * 10000 / ins);
    printf("checksum      %lu\n", (unsigned long)(acc + data[8]));
    printf("hpm_test done\n");
    return 0;
}

// Multicore harness: runs a statically assigned task list on every hart and
// prints per-task cycles and counters plus the workload makespan.
//
// The assignment comes from a generated header (scripts/gen_workloads.py),
// passed in with -DWORKLOAD_HEADER='"path/to/x.h"'. It lists "slots", each
// with a core type and an ordered task list. At run time every hart reads
// marchid, the harts of each type are ranked by hart id, and the k-th hart of
// a type takes the k-th slot of that type. So the harness does not assume
// which hart id is the BOOM (in BudgetSB1R2Config hart 0 is a Rocket).
//
// Output (sent by hart 0 in one block after everyone finishes). Compact,
// because the simulated host link costs thousands of cycles per character:
//   HETERO begin config=... mix=... policy=... nharts=N
//   H hart type(r/b) slot ntasks cycles
//   T hart slot task_id seed reps cycles instret brmiss imiss dmiss chk(hex, 64-bit folded to 32)
//   RUN makespan=C
//   HETERO end status=ok|warn
// task_id indexes task_table (tasks.h); parse_logs.py turns it back into names.

#include <stdio.h>
#include <stdlib.h>
#include "runtime.h"
#include "tasks.h"

typedef struct { int task; uint32_t seed; int reps; } wl_task_t;

#include WORKLOAD_HEADER
// The header defines:
//   WL_CONFIG, WL_MIX, WL_POLICY (strings), WL_NHARTS, WL_NSLOTS, WL_NTASKS
//   static const int wl_slot_type[WL_NSLOTS];        // 0 any, 1 rocket, 2 boom
//   static const int wl_slot_start[WL_NSLOTS + 1];   // offsets into wl_tasks
//   static const wl_task_t wl_tasks[WL_NTASKS];

#if WL_NHARTS != NHARTS
#error "WL_NHARTS in the workload header must match -DNHARTS"
#endif

// The report is built in memory and sent to the host with ONE printstr call:
// every host call goes through the slow simulated host link (~1M+ cycles per
// call), so printing line by line can run into the simulator's cycle limit.
extern void printstr(const char *s);
static char out[32768];
static int outn;
#define OUT(...) do { if (outn < (int)sizeof(out) - 512) \
                          outn += sprintf(out + outn, __VA_ARGS__); } while (0)

typedef struct {
    snap_t d;            // counter deltas for this task
    uint64_t chk;
    int hart;
} rec_t;

static uint8_t scratch[NHARTS][SCRATCH_BYTES]
    __attribute__((section(".noinit"), aligned(64)));

static volatile unsigned long hart_arch[NHARTS];
static volatile int hart_slot[NHARTS];
static volatile uint64_t hart_cycles[NHARTS];
static rec_t recs[WL_NTASKS];

static int find_slot(int cid, unsigned long arch) {
    int rank = 0;                                   // my rank among same-type harts
    for (int h = 0; h < cid; h++)
        if (hart_arch[h] == arch) rank++;
    int seen = 0;
    for (int s = 0; s < WL_NSLOTS; s++) {
        if (wl_slot_type[s] == 0 || (unsigned long)wl_slot_type[s] == arch) {
            if (seen == rank) return s;
            seen++;
        }
    }
    return -1;                                      // no work for this hart
}

void thread_entry(int cid, int nc) {
    unsigned long arch = read_csr(marchid);
    counters_setup(arch);
    hart_arch[cid] = arch;
    barrier(nc);

    int slot = find_slot(cid, arch);
    hart_slot[cid] = slot;
    barrier(nc);                                    // everyone starts together

    snap_t h0, h1, t0, t1;
    snap(&h0);
    if (slot >= 0) {
        for (int i = wl_slot_start[slot]; i < wl_slot_start[slot + 1]; i++) {
            const wl_task_t *t = &wl_tasks[i];
            snap(&t0);
            uint64_t chk = task_table[t->task].fn(scratch[cid], t->seed, t->reps);
            snap(&t1);
            recs[i].d.cycle   = t1.cycle   - t0.cycle;
            recs[i].d.instret = t1.instret - t0.instret;
            recs[i].d.brmiss  = t1.brmiss  - t0.brmiss;
            recs[i].d.imiss   = t1.imiss   - t0.imiss;
            recs[i].d.dmiss   = t1.dmiss   - t0.dmiss;
            recs[i].chk = chk;
            recs[i].hart = cid;
        }
    }
    snap(&h1);
    hart_cycles[cid] = h1.cycle - h0.cycle;
    barrier(nc);

    if (cid != 0)
        while (1) ;                                 // hart 0 reports and exits

    int warn = 0;
    OUT("HETERO begin config=%s mix=%s policy=%s nharts=%d\n",
           WL_CONFIG, WL_MIX, WL_POLICY, nc);
    uint64_t makespan = 0;
    for (int h = 0; h < nc; h++) {
        int s = hart_slot[h];
        int n = s >= 0 ? wl_slot_start[s + 1] - wl_slot_start[s] : 0;
        OUT("H %d %c %d %d %lu\n", h, arch_name(hart_arch[h])[0], s, n,
            (unsigned long)hart_cycles[h]);
        if (hart_cycles[h] > makespan) makespan = hart_cycles[h];
    }
    for (int s = 0; s < WL_NSLOTS; s++) {
        int used = 0;
        for (int h = 0; h < nc; h++) if (hart_slot[h] == s) used = 1;
        if (!used) {
            OUT("WARN slot %d (type %d) had no matching hart: core mix differs from header\n",
                   s, wl_slot_type[s]);
            warn = 1;
        }
        for (int i = wl_slot_start[s]; used && i < wl_slot_start[s + 1]; i++) {
            const wl_task_t *t = &wl_tasks[i];
            const rec_t *r = &recs[i];
            OUT("T %d %d %d %u %d %lu %lu %lu %lu %lu %x\n",
                r->hart, s, t->task, (unsigned)t->seed, t->reps,
                (unsigned long)r->d.cycle, (unsigned long)r->d.instret,
                (unsigned long)r->d.brmiss, (unsigned long)r->d.imiss,
                (unsigned long)r->d.dmiss, (unsigned)(r->chk ^ (r->chk >> 32)));
        }
    }
    OUT("RUN makespan=%lu\n", (unsigned long)makespan);
    OUT("HETERO end status=%s\n", warn ? "warn" : "ok");
    printstr(out);
    exit(0);
}

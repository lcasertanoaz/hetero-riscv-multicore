# Workloads

6 to 8 bare-metal tasks from riscv-tests and Embench-IoT (for example
dhrystone, qsort, vvadd, spmv), grouped into three mixes:

| Mix | Tasks | Why |
| --- | --- | --- |
| Branch-heavy | TBD | Out-of-order execution should recover mispredicted-branch time |
| Memory-heavy | TBD | Both core types wait on memory; small cores should win per LUT |
| Balanced | TBD | Expected sweet spot for a mixed configuration |

`multitask/` runs a statically assigned set of tasks on every hart and reports
per-hart cycles and the workload completion time.

Performance counters to collect per task: cycles, instructions retired, branch
mispredictions, I-cache and D-cache misses. Event encodings for the
`mhpmevent` CSRs are defined in rocket-chip (`RocketCore.scala`, `EventSets`)
and in BOOM's core; look them up for your Chipyard version.

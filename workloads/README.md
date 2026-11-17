# Workloads and multicore harness

Bare-metal programs that run a statically assigned list of tasks on every
core of a config and report per-task cycles, instructions and counters.

## Pieces

| Path | What it is |
| --- | --- |
| `tasks/tasks.c` | 9 task kernels: branch (`qsort`, `brrand`, `median`), memory (`ptrchase`, `stream`, `spmv`), compute (`matmul`, `crc32`, `fir`). Each uses its own 256 KiB scratch buffer and returns a checksum. |
| `harness/harness.c` | Runs on every hart. Reads `marchid`, ranks harts of each core type, takes the matching slot from the generated header, runs its tasks, prints `TASK`/`HART`/`RUN` lines from hart 0. Does not assume which hart is the BOOM. |
| `runtime/crt.S`, `link.ld`, `runtime.h` | Multi-hart startup (riscv-tests' stock `crt.S` lets only hart 0 run), BSS zeroing, scratch section, barrier, counter setup with verified Rocket/BOOM event codes. `printf`/`exit` come from the toolchain's riscv-tests `syscalls.c`. |
| `mixes.json` | Task repetitions and the three mixes (12 task instances each). |
| `build/` | Generated headers and binaries (not in git). |

## Flow (inside the `cy` container, from the repo root)

```bash
cd /workspace/student-work/hetero-riscv-multicore
python3 scripts/gen_workloads.py                 # headers with placeholder estimates
make -C workloads                                # 18 binaries
scripts/run_sweep.sh single                      # task-by-task on R1, SB1, MB1
python3 scripts/parse_logs.py                    # -> data/single_core.csv
python3 scripts/gen_workloads.py --single-core data/single_core.csv
make -C workloads                                # rebuild with real estimates
scripts/run_sweep.sh sweep                       # 15 multicore runs
python3 scripts/parse_logs.py                    # -> results/runs.csv, tasks.csv
```

Check `data/single_core.csv` after the single-core runs: aim for roughly
200k-1M Rocket cycles per task. If a task is far outside that, change its
`reps` in `mixes.json` and redo the single-core step.

## Output format

Hart 0 sends one compact block at the end (the simulated host link costs
thousands of cycles per character, so the report is kept short):

```
HETERO begin config=BudgetSB1R2Config mix=balanced policy=eft nharts=3
H <hart> <type r|b> <slot> <ntasks> <cycles>
T <hart> <slot> <task_id> <seed> <reps> <cycles> <instret> <brmiss> <imiss> <dmiss> <chk>
RUN makespan=<cycles>
HETERO end status=ok
```

`task_id` indexes `task_table` in `tasks/tasks.h`; `chk` is the task's 64-bit
checksum folded to 32 bits (hex). `parse_logs.py` turns this into named CSV
columns (and still reads the older long format). `makespan` = the longest
hart's time from the common start barrier to its last task. `brmiss`, `imiss`,
`dmiss` = branch mispredicts and L1 I/D misses.

## Tested so far

The kernels, the slot assignment and the Python scripts were tested on a
regular PC (threads standing in for harts, with sanitizers). The RISC-V parts
(`crt.S`, `link.ld`, real counters) are only tested once built and run in the
course container.

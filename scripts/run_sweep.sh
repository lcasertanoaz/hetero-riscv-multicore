#!/bin/bash
# Run harness binaries on the Verilator simulators (inside the cy container).
#
#   scripts/run_sweep.sh single            # 1-core characterization: R1, SB1, MB1
#   scripts/run_sweep.sh sweep             # all multicore runs
#   scripts/run_sweep.sh sweep BudgetR4Config   # just one config
#   FORCE=1 scripts/run_sweep.sh sweep     # rerun even if a finished log exists
#   MAXCYC=80000000 scripts/run_sweep.sh single   # raise the simulated-cycle limit
#
# Runs one at a time with LOADMEM=1. A run whose log already ends with
# "HETERO end" is skipped, so you can stop (Ctrl+C) and restart safely.
# Afterwards: python3 scripts/parse_logs.py

set -u
ROLE=${1:-}
ONLY=${2:-}
CY=${CHIPYARD_DIR:-/workspace/chipyard}
REPO=$(cd "$(dirname "$0")/.." && pwd)
SIM=$CY/sims/verilator
# Chipyard's default limit is 10M simulated cycles; single-core runs need more.
# (Both spellings of the make variable are passed; Chipyard uses one of them.)
MAXCYC=${MAXCYC:-40000000}

case "$ROLE" in
  single) CONFIGS="BudgetR1Config BudgetSB1Config BudgetMB1Config"; PATTERN="single.riscv" ;;
  sweep)  CONFIGS="BudgetR4Config BudgetSB1R2Config BudgetSB2Config BudgetMB1Config"; PATTERN="*.*.riscv" ;;
  *) echo "usage: $0 single|sweep [Config]"; exit 1 ;;
esac
[ -n "$ONLY" ] && CONFIGS=$ONLY

total_start=$SECONDS
for cfg in $CONFIGS; do
  for bin in "$REPO"/workloads/build/$cfg/$PATTERN; do
    [ -e "$bin" ] || { echo "no binaries for $cfg (run gen_workloads.py and make -C workloads)"; continue; }
    name=$(basename "$bin" .riscv)
    log=$SIM/output/chipyard.harness.TestHarness.$cfg/$name.log
    if [ -z "${FORCE:-}" ] && [ -f "$log" ] && grep -q "HETERO end" "$log"; then
      echo "skip  $cfg/$name (already done)"; continue
    fi
    s=$SECONDS
    echo -n "run   $cfg/$name ... "
    make -C "$SIM" CONFIG=$cfg run-binary-fast LOADMEM=1 \
      timeout_cycles=$MAXCYC TIMEOUT_CYCLES=$MAXCYC BINARY="$bin" > /dev/null 2>&1
    if grep -q "HETERO end status=ok" "$log" 2>/dev/null; then
      echo "ok  ($((SECONDS - s)) s, $(grep -o 'makespan=[0-9]*' "$log"))"
    else
      echo "FAILED ($((SECONDS - s)) s) - see $log"
    fi
  done
done
echo "done in $(( (SECONDS - total_start) / 60 )) min. Next: python3 scripts/parse_logs.py"

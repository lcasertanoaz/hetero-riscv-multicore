#!/usr/bin/env python3
"""Predict multicore completion time from single-core measurements.

Reads a CSV of per-task single-core cycle counts on Rocket and on BOOM, assigns
tasks statically to the cores of each mix, and predicts the time to finish the
whole workload (the busiest core's total). Comparing these predictions with
real multicore runs shows how much shared-memory contention matters.

Policies:
  speedup  (as in the proposal) tasks with the largest BOOM speedup go to BOOM
           cores first; within a core type, each task goes to the least-loaded
           core (longest task first).
  eft      earliest finish time: longest tasks first, each placed on whichever
           core (BOOM or Rocket) would finish it soonest. A load-balancing
           comparison point for the proposal's rule.

CSV columns: task,rocket_cycles,boom_cycles
Example:  python scripts/predict.py data/single_core_EXAMPLE.csv --mixes 0:4 1:2 2:0
"""
import argparse, csv, math


def assign(tasks, n_boom, n_rocket):
    """tasks: list of (name, rocket_cycles, boom_cycles). Returns per-core task lists and loads."""
    by_speedup = sorted(tasks, key=lambda t: t[1] / t[2], reverse=True)
    n_cores = n_boom + n_rocket
    n_to_boom = 0 if n_boom == 0 else (len(tasks) if n_rocket == 0 else
                                       min(len(tasks), n_boom * math.ceil(len(tasks) / n_cores)))
    groups = [("boom", n_boom, by_speedup[:n_to_boom], 2), ("rocket", n_rocket, by_speedup[n_to_boom:], 1)]
    cores = []
    for kind, n, group, col in groups:
        if n == 0:
            continue
        loads = [[0, []] for _ in range(n)]
        for t in sorted(group, key=lambda t: t[col], reverse=True):     # longest first
            core = min(loads, key=lambda c: c[0])
            core[0] += t[col]
            core[1].append(t[0])
        cores += [(kind, c[0], c[1]) for c in loads]
    return cores


def assign_eft(tasks, n_boom, n_rocket):
    cores = [["boom", 0, []] for _ in range(n_boom)] + [["rocket", 0, []] for _ in range(n_rocket)]
    for name, rc, bc in sorted(tasks, key=lambda t: max(t[1], t[2]), reverse=True):
        best = min(cores, key=lambda c: c[1] + (bc if c[0] == "boom" else rc))
        best[1] += bc if best[0] == "boom" else rc
        best[2].append(name)
    return [tuple(c) for c in cores]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--mixes", nargs="+", default=["0:4", "1:2", "2:0"],
                    help="BOOM:Rocket core counts, e.g. 1:2")
    ap.add_argument("--policy", choices=["speedup", "eft", "both"], default="both")
    a = ap.parse_args()
    tasks = [(r["task"], int(r["rocket_cycles"]), int(r["boom_cycles"])) for r in csv.DictReader(open(a.csv))]

    print(f"{'task':>10} {'Rocket':>10} {'BOOM':>10} {'speedup':>8}")
    for name, rc, bc in tasks:
        print(f"{name:>10} {rc:>10} {bc:>10} {rc / bc:>8.2f}")
    print()
    policies = ["speedup", "eft"] if a.policy == "both" else [a.policy]
    for mix in a.mixes:
        b, r = (int(x) for x in mix.split(":"))
        for pol in policies:
            cores = assign(tasks, b, r) if pol == "speedup" else assign_eft(tasks, b, r)
            makespan = max(c[1] for c in cores)
            print(f"{b} BOOM + {r} Rocket [{pol}]: predicted completion {makespan:,} cycles")
            for kind, load, names in cores:
                print(f"    {kind:6} {load:>12,}  {', '.join(names)}")


if __name__ == "__main__":
    main()

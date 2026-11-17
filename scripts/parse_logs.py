#!/usr/bin/env python3
"""Turn simulator logs into CSVs.

  python3 scripts/parse_logs.py
  python3 scripts/parse_logs.py --output /workspace/chipyard/sims/verilator/output

Reads <output>/chipyard.harness.TestHarness.<Config>/<mix>.<policy>.log and
.../single.log (written by run-binary-fast for our binaries), and writes:

  results/raw/<Config>__<name>.txt   copy of each log (*.log is gitignored)
  results/tasks.csv    one row per task instance, with IPC and MPKI
  results/runs.csv     one row per run: measured vs predicted makespan
  data/single_core.csv per-task cycles on rocket/smallboom/mediumboom,
                       the input to gen_workloads.py --single-core

It also checks that every (task, seed, reps) gave the same checksum on every
core and config; a mismatch means a broken run.
"""
import argparse, csv, glob, os, re, shutil, sys
from collections import defaultdict

BIG = {"BudgetSB1Config": "smallboom", "BudgetMB1Config": "mediumboom", "BudgetR1Config": "rocket"}
KV = re.compile(r"(\w+)=(\S+)")


def kv(line):
    return dict(KV.findall(line))


TASK_NAMES = ["qsort", "brrand", "median", "ptrchase", "stream", "spmv",
              "matmul", "crc32", "fir"]          # same order as tasks.h
TYPES = {"r": "rocket", "b": "boom", "u": "unknown"}
T_FIELDS = ["hart", "slot", "task", "seed", "reps", "cycles", "instret",
            "brmiss", "imiss", "dmiss", "chk"]


def parse_log(path):
    """Reads both report formats: compact (H/T lines) and the first long one."""
    run, tasks, harts = {}, [], []
    with open(path, errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("HETERO begin"):
                run.update(kv(line))
            elif line.startswith("HETERO end"):
                run["status"] = kv(line).get("status", "?")
            elif line.startswith("RUN "):
                run["makespan"] = int(kv(line)["makespan"])
            elif line.startswith("HART "):
                harts.append(kv(line))
            elif line.startswith("TASK "):
                tasks.append(kv(line))
            elif line.startswith("H "):
                p = line.split()
                if len(p) == 6:
                    harts.append({"hart": p[1], "type": TYPES.get(p[2], p[2]), "slot": p[3],
                                  "ntasks": p[4], "cycles": p[5]})
            elif line.startswith("T "):
                p = line.split()
                if len(p) == 12:
                    t = dict(zip(T_FIELDS, p[1:]))
                    t["task"] = TASK_NAMES[int(t["task"])]
                    tasks.append(t)
    hart_type = {h["hart"]: h["type"] for h in harts}
    for t in tasks:
        t.setdefault("type", hart_type.get(t["hart"], "unknown"))
        # the compact format prints the 64-bit checksum folded to 32 bits; fold old logs too
        c = int(t["chk"], 16)
        t["chk"] = f"{(c ^ (c >> 32)) & 0xffffffff if c >> 32 else c:08x}"
    return run, harts, tasks


def big_flavor(config):
    if "MB" in config:
        return "mediumboom"
    if "SB" in config:
        return "smallboom"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", default="/workspace/chipyard/sims/verilator/output")
    ap.add_argument("--predictions", default="workloads/build/predictions.csv")
    a = ap.parse_args()

    logs = sorted(glob.glob(os.path.join(a.output, "chipyard.harness.TestHarness.Budget*", "*.log")))
    os.makedirs("results/raw", exist_ok=True)
    os.makedirs("data", exist_ok=True)

    preds = {}
    if os.path.exists(a.predictions):
        for r in csv.DictReader(open(a.predictions)):
            preds[(r["config"], r["mix"], r["policy"])] = (int(r["predicted"]), r["estimates"])

    task_rows, run_rows = [], []
    single = defaultdict(dict)       # task -> {core: row}
    checks = defaultdict(set)        # (task, seed, reps) -> {chk}
    n_ok = n_bad = 0
    for path in logs:
        config = os.path.basename(os.path.dirname(path)).split(".")[-1]
        name = os.path.splitext(os.path.basename(path))[0]
        run, harts, tasks = parse_log(path)
        if not run.get("config"):
            continue                 # not one of our harness logs (e.g. dhrystone)
        shutil.copy(path, f"results/raw/{config}__{name}.txt")
        ok = run.get("status") == "ok" and "makespan" in run
        n_ok += ok; n_bad += not ok
        if not ok:
            print(f"WARNING: {config}/{name}: status={run.get('status', 'missing (run cut short?)')}")
        flavor = big_flavor(config)
        for t in tasks:
            ins = max(1, int(t["instret"]))
            row = {"config": config, "mix": run["mix"], "policy": run["policy"],
                   "hart": t["hart"],
                   "core": (flavor if t["type"] == "boom" else "rocket"),
                   "task": t["task"], "seed": t["seed"], "reps": t["reps"],
                   "cycles": t["cycles"], "instret": t["instret"],
                   "ipc": f"{ins / max(1, int(t['cycles'])):.3f}",
                   "br_mpki": f"{1000 * int(t['brmiss']) / ins:.2f}",
                   "imiss_mpki": f"{1000 * int(t['imiss']) / ins:.2f}",
                   "dmiss_mpki": f"{1000 * int(t['dmiss']) / ins:.2f}",
                   "chk": t["chk"]}
            task_rows.append(row)
            checks[(t["task"], t["seed"], t["reps"])].add(t["chk"])
            if run["mix"] == "single":
                single[t["task"]][row["core"]] = row
        if run["mix"] != "single" and ok:
            pred, est = preds.get((config, run["mix"], run["policy"]), (None, ""))
            ms = run["makespan"]
            run_rows.append({"config": config, "mix": run["mix"], "policy": run["policy"],
                             "makespan": ms, "predicted": pred or "",
                             "contention": f"{ms / pred:.3f}" if pred else "",
                             "estimates": est, "nharts": run.get("nharts", "")})

    if task_rows:
        with open("results/tasks.csv", "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(task_rows[0])); w.writeheader(); w.writerows(task_rows)
    if run_rows:
        with open("results/runs.csv", "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(run_rows[0])); w.writeheader(); w.writerows(run_rows)
    if single:
        cols = ["task", "reps", "rocket", "smallboom", "mediumboom",
                "sb_speedup", "mb_speedup", "rocket_br_mpki", "rocket_dmiss_mpki",
                "smallboom_br_mpki", "smallboom_dmiss_mpki"]
        with open("data/single_core.csv", "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=cols); w.writeheader()
            for task, by in single.items():
                r = {"task": task, "reps": next(iter(by.values()))["reps"]}
                for core in ("rocket", "smallboom", "mediumboom"):
                    if core in by:
                        r[core] = by[core]["cycles"]
                rc = int(r.get("rocket") or 0)
                if rc and r.get("smallboom"):
                    r["sb_speedup"] = f"{rc / int(r['smallboom']):.2f}"
                if rc and r.get("mediumboom"):
                    r["mb_speedup"] = f"{rc / int(r['mediumboom']):.2f}"
                for core in ("rocket", "smallboom"):
                    if core in by:
                        r[f"{core}_br_mpki"] = by[core]["br_mpki"]
                        r[f"{core}_dmiss_mpki"] = by[core]["dmiss_mpki"]
                w.writerow(r)

    bad = {k: v for k, v in checks.items() if len(v) > 1}
    print(f"{len(logs)} logs scanned: {n_ok} harness runs ok, {n_bad} not ok; "
          f"{len(task_rows)} task rows, {len(run_rows)} sweep runs, {len(single)} single-core tasks")
    if bad:
        print("CHECKSUM MISMATCH (same task/seed/reps, different result):")
        for k, v in bad.items():
            print("   ", k, sorted(v))
        sys.exit(1)
    print("checksums consistent")


if __name__ == "__main__":
    main()

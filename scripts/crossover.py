#!/usr/bin/env python3
"""Parallelism crossover (figure 5): which design wins as the task count grows?

Run from the repo root:
  python3 scripts/crossover.py                 # N = 1..8 copies of fir
  python3 scripts/crossover.py --task qsort    # same question for another task

For N = 1..max_n identical copies of one task, it predicts the makespan of every
sweep design with the same static scheduler the real runs use (eft from
predict.py, fed with the single-core cycle counts). No interference is modeled,
so these are the same kind of predictions as results/runs.csv's "predicted".

Measured points are drawn on top when they exist:
  N = 1      from data/single_core.csv (a single task runs alone on the fastest
             core, so the single-core run is the measurement)
  N >= 2     from results/runs.csv rows whose mix is "<task><N>", e.g. fir2,
             fir4 (add those mixes to workloads/mixes.json and run them)

Writes results/crossover.csv and results/figures/fig5_crossover.pdf/.png.
"""
import argparse, csv, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import predict                      # noqa: E402  same schedulers as the real runs
import gen_workloads as gw          # noqa: E402  design list + single-core loading
import analyze                      # noqa: E402  colors, plot style, save()
import matplotlib.pyplot as plt     # noqa: E402

SHORT = {"BudgetR4Config": "R4", "BudgetSB1R2Config": "SB1R2",
         "BudgetSB2Config": "SB2", "BudgetMB1Config": "MB1"}


def sweep_designs():
    """[(short name, big core type, n_boom, n_rocket)] for the four sweep designs."""
    out = []
    for cfg, (big, nb, nr, roles) in gw.CONFIGS.items():
        if "sweep" in roles:
            out.append((SHORT[cfg], big, nb, nr))
    order = analyze.CONFIGS                       # R4, SB1R2, SB2, MB1
    return sorted(out, key=lambda d: order.index(d[0]))


def predict_makespan(est, task, n, big, nb, nr):
    """Predicted cycles to finish n copies of `task` on one design (eft schedule)."""
    rocket = int(est[task]["rocket"])
    boom = int(est[task][big]) if big else rocket   # unused when nb == 0
    copies = [(f"{task}#{i}", rocket, boom) for i in range(n)]
    cores = predict.assign_eft(copies, nb, nr)
    return max(load for _, load, _ in cores)


def measured_points(task, designs, single_csv, runs_csv):
    """{design: {n: cycles}} from the single-core runs (n=1) and the sweep (n>=2)."""
    pts = {d[0]: {} for d in designs}
    sc = {r["task"]: r for r in csv.DictReader(open(single_csv))}
    if task in sc:
        row = sc[task]
        for name, big, nb, nr in designs:
            # one task goes to the design's fastest core type
            core = big if nb and big else "rocket"
            if row.get(core):
                pts[name][1] = int(row[core])
    if os.path.exists(runs_csv):
        for r in csv.DictReader(open(runs_csv)):
            mix, name = r["mix"], SHORT.get(r["config"], r["config"])
            if mix.startswith(task) and mix[len(task):].isdigit() and r["policy"] == "eft":
                pts.setdefault(name, {})[int(mix[len(task):])] = int(r["makespan"])
    return pts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--task", default="fir")
    ap.add_argument("--max-n", type=int, default=8)
    ap.add_argument("--single-core", default="data/single_core.csv")
    ap.add_argument("--runs", default="results/runs.csv")
    a = ap.parse_args()

    reps = {a.task: 1}
    est, real = gw.load_estimates(a.single_core, reps)
    if not real or a.task not in est:
        sys.exit(f"no single-core data for {a.task} in {a.single_core}")
    designs = sweep_designs()
    ns = list(range(1, a.max_n + 1))

    # predicted makespan table, design x N
    pred = {name: [predict_makespan(est, a.task, n, big, nb, nr) for n in ns]
            for name, big, nb, nr in designs}

    print(f"Predicted makespan (M cycles), {a.task} x N, eft schedule:")
    print(f"{'N':>3} " + " ".join(f"{d[0]:>7}" for d in designs) + "   best")
    rows = []
    for i, n in enumerate(ns):
        vals = {d[0]: pred[d[0]][i] for d in designs}
        low = min(vals.values())
        best = "=".join(d for d in vals if vals[d] == low)     # ties shown as R4=SB1R2
        print(f"{n:>3} " + " ".join(f"{vals[d[0]] / 1e6:>7.3f}" for d in designs) + f"   {best}")
        rows.append({"task": a.task, "n": n, **vals, "best": best})

    os.makedirs("results", exist_ok=True)
    with open("results/crossover.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
    print("wrote results/crossover.csv")

    meas = measured_points(a.task, designs, a.single_core, a.runs)

    # ---- figure 5 ------------------------------------------------------------
    fig, ax = plt.subplots(figsize=(3.5, 2.6))
    for name, *_ in designs:
        c = analyze.COLOR[name]
        # SB1R2 often ties R4 or SB2, so draw it dashed and on top to keep it visible
        ls, z = ("--", 2.5) if name == "SB1R2" else ("-", 2)
        ax.plot(ns, [v / 1e6 for v in pred[name]], color=c, lw=1.6, ls=ls,
                marker="o", ms=3.5, label=name, zorder=z)
        mx = sorted(meas.get(name, {}).items())
        if mx:                                       # measured: hollow, bigger, on top
            ax.scatter([n for n, _ in mx], [v / 1e6 for _, v in mx], s=46,
                       facecolor="white", edgecolor=c, linewidth=1.5, zorder=3)
    # one legend entry explaining the hollow markers
    ax.scatter([], [], s=46, facecolor="white", edgecolor="#52514e",
               linewidth=1.5, label="measured")
    ax.set_xticks(ns)
    ax.set_xlabel(f"Number of {a.task} tasks")
    ax.set_ylabel("Makespan (M cycles)\n(lower is better)")
    ax.set_ylim(bottom=0)
    ax.legend(ncol=5, loc="upper center", bbox_to_anchor=(0.5, 1.2),
              handlelength=1.2, columnspacing=0.8, fontsize=7)
    analyze.save(fig, "fig5_crossover" if a.task == "fir" else f"fig5_crossover_{a.task}")


if __name__ == "__main__":
    main()

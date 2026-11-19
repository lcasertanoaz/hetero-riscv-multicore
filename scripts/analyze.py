#!/usr/bin/env python3
"""Make the paper figures from the parsed results.

Run from the repo root:  python3 scripts/analyze.py
Reads  results/runs.csv, data/single_core.csv
Writes results/figures/fig1..fig4 as .pdf (for the paper) and .png (to look at)
"""
import os
import pandas as pd
import matplotlib
matplotlib.use("Agg")            # no display needed (WSL / container)
import matplotlib.pyplot as plt

OUT = "results/figures"

# ---- fixed orders and names, used by every figure ---------------------------
CONFIGS = ["R4", "SB1R2", "SB2", "MB1"]
MIXES = ["branch", "memory", "balanced", "few"]
TASKS = ["qsort", "brrand", "median",        # branch
         "ptrchase", "stream", "spmv",       # memory
         "matmul", "crc32", "fir"]           # compute
KINDS = ["branch", "memory", "compute"]      # three tasks each, in TASKS order

COLOR = {"R4": "#2a78d6", "SB1R2": "#eb6834", "SB2": "#1baf7a", "MB1": "#eda100"}
MARKER = {"branch": "o", "memory": "s", "balanced": "^", "few": "D"}

# ---- constants that come from other files -----------------------------------
# LUTs per tile, from data/tile_area.csv (Vivado, out-of-context)
TILE_LUTS = {"rocket": 29569, "smallboom": 66361, "mediumboom": 117628}
CORES = {"R4": {"rocket": 4},
         "SB1R2": {"smallboom": 1, "rocket": 2},
         "SB2": {"smallboom": 2},
         "MB1": {"mediumboom": 1}}
CONFIG_LUTS = {c: sum(TILE_LUTS[t] * n for t, n in CORES[c].items()) for c in CONFIGS}
# tasks per mix, from workloads/mixes.json
NTASKS = {"branch": 12, "memory": 12, "balanced": 12, "few": 3}

plt.rcParams.update({
    "font.size": 8, "axes.spines.top": False, "axes.spines.right": False,
    "axes.grid": True, "axes.grid.axis": "y", "grid.color": "#e4e3df",
    "grid.linewidth": 0.6, "axes.axisbelow": True, "legend.frameon": False,
    "pdf.fonttype": 42,          # embed real fonts (IEEE PDF checkers want this)
})


def save(fig, name):
    os.makedirs(OUT, exist_ok=True)
    fig.savefig(f"{OUT}/{name}.pdf", bbox_inches="tight")
    fig.savefig(f"{OUT}/{name}.png", bbox_inches="tight", dpi=200)
    plt.close(fig)
    print("wrote", f"{OUT}/{name}.pdf/.png")


def load_runs():
    df = pd.read_csv("results/runs.csv")
    # BudgetSB1R2Config -> SB1R2
    df["config"] = df["config"].str.replace("Budget", "").str.replace("Config", "")
    return df


def grouped_bars(ax, table, colors):
    """table: rows = x groups, columns = series. One bar per cell."""
    n = len(table.columns)
    w = 0.8 / n
    for i, col in enumerate(table.columns):
        xs = [x - 0.4 + w * (i + 0.5) for x in range(len(table))]
        ax.bar(xs, table[col], w * 0.92, label=col, color=colors[col])
    ax.set_xticks(range(len(table)))
    ax.set_xticklabels(table.index)
    ax.tick_params(axis="x", length=0)


def best_per_mix(df, column="makespan"):
    """mix x config table using the better (lower makespan) policy per cell."""
    best = df.loc[df.groupby(["mix", "config"])["makespan"].idxmin()]
    return best.pivot(index="mix", columns="config", values=column).loc[MIXES, CONFIGS]


# ---- Fig 1: single-core BOOM speedup per task, with miss rates --------------
def fig1():
    sc = pd.read_csv("data/single_core.csv").set_index("task").loc[TASKS]
    fig, (a, b) = plt.subplots(2, 1, figsize=(7, 4), sharex=True,
                               gridspec_kw={"height_ratios": [3, 2]})

    speed = sc[["sb_speedup", "mb_speedup"]]
    speed.columns = ["SmallBOOM", "MediumBOOM"]
    grouped_bars(a, speed, {"SmallBOOM": "#2a78d6", "MediumBOOM": "#eb6834"})
    a.axhline(1, color="#0b0b0b", lw=0.8)            # 1.0 = same speed as Rocket
    a.set_ylabel("Speedup over Rocket")
    a.legend(ncol=2, loc="upper left")

    mpki = sc[["rocket_br_mpki", "rocket_dmiss_mpki"]]
    mpki.columns = ["Branch mispredicts", "D-cache misses"]
    grouped_bars(b, mpki, {"Branch mispredicts": "#1baf7a", "D-cache misses": "#eda100"})
    b.set_ylabel("Rocket MPKI")
    b.legend(ncol=2, loc="upper right")

    for ax in (a, b):                                # separate the three kinds
        for x in (2.5, 5.5):
            ax.axvline(x, color="#c9c8c2", lw=0.6)
    top = a.get_ylim()[1]
    for i, kind in enumerate(KINDS):
        a.text(3 * i + 1, top, kind, ha="center", va="bottom", color="#52514e")
    save(fig, "fig1_single_core_speedup")


# ---- Fig 2: makespan per mix, normalized to R4 ------------------------------
def fig2(df):
    best = best_per_mix(df)
    norm = best.div(best["R4"], axis=0)
    fig, ax = plt.subplots(figsize=(3.5, 2.4))
    grouped_bars(ax, norm, COLOR)
    ax.axhline(1, color="#0b0b0b", lw=0.8)
    ax.set_ylabel("Makespan relative to R4\n(lower is better)")
    ax.legend(ncol=4, loc="upper center", bbox_to_anchor=(0.5, 1.18),
              handlelength=1, columnspacing=1)
    save(fig, "fig2_makespan_norm")


# ---- Fig 3: throughput per area ---------------------------------------------
def fig3(df):
    best = best_per_mix(df)
    tput = pd.DataFrame({c: [NTASKS[m] / (best.loc[m, c] / 1e6) / (CONFIG_LUTS[c] / 1e5)
                             for m in MIXES] for c in CONFIGS}, index=MIXES)
    fig, ax = plt.subplots(figsize=(3.5, 2.4))
    grouped_bars(ax, tput, COLOR)
    ax.set_ylabel("Tasks / M cycles / 100k LUTs\n(higher is better)")
    ax.legend(ncol=4, loc="upper center", bbox_to_anchor=(0.5, 1.18),
              handlelength=1, columnspacing=1)
    save(fig, "fig3_throughput_per_area")
    return tput


# ---- Fig 4: predicted vs measured makespan ----------------------------------
def fig4(df):
    fig, ax = plt.subplots(figsize=(3.5, 3.5))
    lim = [0, max(df["makespan"].max(), df["predicted"].max()) / 1e6 * 1.05]
    ax.plot(lim, lim, color="#52514e", lw=0.8, ls="--", zorder=1)   # y = x (say so in the caption)
    for _, r in df.iterrows():
        ax.scatter(r["predicted"] / 1e6, r["makespan"] / 1e6, s=28, zorder=2,
                   color=COLOR[r["config"]], marker=MARKER[r["mix"]],
                   edgecolor="white", linewidth=0.6)
    # two legends: color = config, shape = mix
    h1 = [plt.Line2D([], [], ls="", marker="o", color=COLOR[c], label=c) for c in CONFIGS]
    h2 = [plt.Line2D([], [], ls="", marker=MARKER[m], color="#52514e", label=m) for m in MIXES]
    ax.add_artist(ax.legend(handles=h1, loc="upper left", title="Config"))
    ax.legend(handles=h2, loc="lower right", title="Mix")
    ax.set_xlim(lim); ax.set_ylim(lim); ax.set_aspect("equal")
    ax.grid(True, axis="both")
    ax.set_xlabel("Predicted makespan (M cycles)")
    ax.set_ylabel("Measured makespan (M cycles)")
    save(fig, "fig4_predicted_vs_measured")


if __name__ == "__main__":
    runs = load_runs()
    fig1()
    fig2(runs)
    print(fig3(runs).round(2))       # should match the throughput table
    fig4(runs)

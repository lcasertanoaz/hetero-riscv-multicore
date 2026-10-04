#!/usr/bin/env python3
"""Enumerate Rocket/BOOM core mixes that fit a fixed LUT budget.

Inputs come from Vivado synthesis of one Rocket tile and one BOOM tile (LUTs
per tile) and the budget you choose (for example, the LUTs of the largest
Rocket-only system that fits, minus the shared uncore).

Example:
  python scripts/budget.py --rocket-luts 8000 --boom-luts 30000 --budget 64000
(those numbers are illustrative only; use your synthesis results)
"""
import argparse


def mixes(rocket, boom, budget):
    out = []
    for b in range(budget // boom + 1):
        left = budget - b * boom
        r = left // rocket
        if b + r == 0:
            continue
        used = b * boom + r * rocket
        out.append({"boom": b, "rocket": r, "cores": b + r, "luts_used": used,
                    "utilization": used / budget})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rocket-luts", type=int, required=True)
    ap.add_argument("--boom-luts", type=int, required=True)
    ap.add_argument("--budget", type=int, required=True, help="LUTs available for cores")
    a = ap.parse_args()
    print(f"BOOM tile = {a.boom_luts / a.rocket_luts:.1f} Rocket tiles; budget {a.budget} LUTs\n")
    print(f"{'BOOM':>4} {'Rocket':>6} {'cores':>5} {'LUTs used':>10} {'used':>6}")
    for m in mixes(a.rocket_luts, a.boom_luts, a.budget):
        print(f"{m['boom']:>4} {m['rocket']:>6} {m['cores']:>5} {m['luts_used']:>10} {m['utilization']:>6.0%}")


if __name__ == "__main__":
    main()

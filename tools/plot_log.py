#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = ["matplotlib"]
# ///
"""Plot a recording from the gate's web UI (Record → Stop and save).

    uv run tools/plot_log.py gate-2026-10-23T....csv            # opens a window
    uv run tools/plot_log.py gate-....csv -o out.png            # saves instead

Top: each channel's top and bottom levels over time, with a line at every
pass coloured by its verdict. Bottom: top − bottom (dB) of every paired pass
by verdict, with the delta limit, which is what the field test is for:
VALID passes through the middle and OVER/UNDER passes should fall in clearly
separate groups. GPL-3.0-or-later.
"""
import argparse
import csv
import json
from collections import defaultdict

import matplotlib.pyplot as plt

COLOURS = {"VALID": "tab:green", "OVER": "tab:red", "UNDER": "tab:purple",
           "SINGLE_TOP": "tab:gray", "SINGLE_BOTTOM": "tab:gray"}


def load(path):
    levels = defaultdict(lambda: ([], [], []))  # ch -> (ms, top, bottom)
    passes = []
    settings = {}
    with open(path, newline="") as f:
        for row in csv.reader(f):
            if not row:
                continue
            if row[0].startswith("#"):
                text = ",".join(row)
                if "settings " in text:
                    try:
                        settings = json.loads(text.split("settings ", 1)[1].replace(";", ","))
                    except ValueError:
                        pass
                continue
            kind = row[0]
            if kind == "lv":
                ms, ch, top, bot = int(row[1]), row[2], int(row[3]), int(row[4])
                t = levels[ch]
                t[0].append(ms / 1000)
                t[1].append(top or float("nan"))
                t[2].append(bot or float("nan"))
            elif kind == "pass":
                passes.append({"s": int(row[1]) / 1000, "ch": row[2], "v": row[3], "top": int(row[4]),
                               "bot": int(row[5]), "d": float(row[6]), "counted": row[7] in ("1", "true")})
    return levels, passes, settings


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv")
    ap.add_argument("-o", "--output", help="save to this image instead of showing a window")
    args = ap.parse_args()
    levels, passes, settings = load(args.csv)
    chans = list(levels) or sorted({p["ch"] for p in passes})
    if not chans:
        raise SystemExit("nothing to plot")

    fig, axes = plt.subplots(len(chans) + 1, 1, figsize=(12, 2.4 * len(chans) + 3.5),
                             gridspec_kw={"height_ratios": [1] * len(chans) + [1.4]})
    for ax, ch in zip(axes, chans):
        ms, top, bot = levels[ch]
        ax.plot(ms, top, color="tab:orange", lw=1, label="top")
        ax.plot(ms, bot, color="tab:blue", lw=1, label="bottom")
        for key, style in (("enter", "--"), ("exit", ":")):
            if key in settings:
                ax.axhline(settings[key], color="0.5", ls=style, lw=0.8)
        for p in passes:
            if p["ch"] == ch:
                ax.axvline(p["s"], color=COLOURS.get(p["v"], "k"), lw=1.5, alpha=0.8)
        ax.set_ylabel(f"{ch}\nlevel code")
        ax.legend(loc="upper right", fontsize=8)
    axes[len(chans) - 1].set_xlabel("gate time, s")

    ax = axes[-1]
    paired = [p for p in passes if not p["v"].startswith("SINGLE")]
    for verdict in ("VALID", "OVER", "UNDER"):
        ds = [p["d"] for p in paired if p["v"] == verdict]
        if ds:
            ax.hist(ds, bins=30, alpha=0.6, color=COLOURS[verdict], label=f"{verdict} ({len(ds)})")
    if "delta" in settings:
        for sign in (-1, 1):
            ax.axvline(sign * settings["delta"] / 4, color="k", ls="--", lw=1)
    singles = len(passes) - len(paired)
    ax.set_xlabel(f"top − bottom, dB (offset applied)   ·   {singles} single-board passes not shown")
    ax.set_ylabel("passes")
    ax.legend(fontsize=8)
    fig.tight_layout()
    if args.output:
        fig.savefig(args.output, dpi=120)
    else:
        plt.show()


if __name__ == "__main__":
    main()

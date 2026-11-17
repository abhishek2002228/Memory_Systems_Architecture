#!/usr/bin/env python3
"""Summarise results/ : 1-core speedup over LRU and 4-core weighted speedup."""
import math, re, sys
from pathlib import Path

R = Path(__file__).resolve().parent.parent / "results"
POL = ["ship_pp", "custom"]
TR = ["bzip2", "libquantum", "mcf", "xalancbmk"]
MIX = {"mix1": ["mcf", "mcf", "libquantum", "libquantum"],
       "mix2": ["mcf", "libquantum", "mcf", "xalancbmk"]}

def ipc(f):
    text = f.read_text()
    text = text.split("Region of Interest Statistics")[-1]  # multicore: per-core first-10M stats
    v = [float(x) for x in re.findall(r"^CPU \d+ cumulative IPC: ([\d.]+)", text, re.M)]
    if not v: sys.exit(f"incomplete run: {f}")
    return v

gm = lambda xs: math.exp(sum(map(math.log, xs)) / len(xs))
alone = {}
for pf, name in [("no-no", "no prefetch"), ("next_line-ip_stride", "L1D next_line + L2C ip_stride")]:
    print(f"\n1-core, {name}")
    print(f"{'trace':12}{'lru':>10}" + "".join(f"{p:>10}{'x':>8}" for p in POL))
    sp = {p: [] for p in POL}
    for t in TR:
        row = {p: ipc(R / "1core" / f"{t}-{pf}-{p}.txt")[0] for p in ["lru_crc2"] + POL}
        alone[(pf, t)] = row
        line = f"{t:12}{row['lru_crc2']:10.4f}"
        for p in POL:
            s = row[p] / row["lru_crc2"]; sp[p].append(s); line += f"{row[p]:10.4f}{s:8.4f}"
        print(line)
    print(f"{'geomean':22}" + "".join(f"{'':10}{gm(sp[p]):8.4f}" for p in POL))

pf = "next_line-ip_stride"
if (R / "4core").exists():
    print("\n4-core weighted speedup over LRU (WS = sum IPC_shared/IPC_alone, own-policy IPC_alone)")
    res = {p: [] for p in POL}
    for m, traces in MIX.items():
        ws = {}
        for p in ["lru_crc2"] + POL:
            shared = ipc(R / "4core" / f"{m}-{p}.txt")
            ws[p] = sum(s / alone[(pf, t)][p] for s, t in zip(shared, traces))
        line = f"{m} [{' + '.join(traces)}]:"
        for p in POL:
            res[p].append(ws[p] / ws["lru_crc2"]); line += f"  {p} {res[p][-1]:.4f}x"
        print(line)
    print("geomean:" + "".join(f"  {p} {gm(res[p]):.4f}x" for p in POL))

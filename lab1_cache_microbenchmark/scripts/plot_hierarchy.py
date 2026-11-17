import pandas as pd
import matplotlib.pyplot as plt

import sys
df = pd.read_csv(sys.argv[1] if len(sys.argv) > 1 else "../data/cache_hierarchy_2.csv")

plt.figure()
plt.plot(df["Array_Size_KB"], df["Latency_ns"], marker="o")

# Vertical lines at specific sizes (KB)
for x in [32, 1024, 19712]:
    plt.axvline(x=x, linestyle="--", linewidth=1.5)
    # Optional: label the line
    plt.text(x, plt.ylim()[1], f"{x} KB", rotation=90, va="top", ha="right")

plt.xscale("log")
plt.xlabel("Array Size (KB, log scale)")
plt.ylabel("Latency (ns)")
plt.title("Cache Hierarchy")
plt.grid(True, which="both")

plt.show()

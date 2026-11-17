import sys
import pandas as pd
import matplotlib.pyplot as plt

if len(sys.argv) != 2:
    print("Usage: python plot_hit_latency.py cache_hit_latencies.csv")
    sys.exit(1)

df = pd.read_csv(sys.argv[1])

plt.figure(figsize=(5,4))
plt.bar(df["level"], df["avg_cycles"])
plt.ylabel("Average cycles")
plt.title("Cache Hit Latencies")
plt.grid(axis="y")
plt.tight_layout()
plt.show()

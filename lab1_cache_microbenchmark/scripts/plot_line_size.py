import sys
import pandas as pd
import matplotlib.pyplot as plt

if len(sys.argv) != 2:
    print("Usage: python plot_line_size.py cache_line_size.csv")
    sys.exit(1)

csv = sys.argv[1]

df = pd.read_csv(csv)

plt.figure(figsize=(7,4))
plt.plot(df["delta_bytes"], df["avg_cycles"], marker="o")
plt.xlabel("Delta (bytes)")
plt.ylabel("Average cycles")
plt.title("Cache Line Size Detection (neighbor-hit test)")
plt.grid(True)
plt.tight_layout()
plt.show()

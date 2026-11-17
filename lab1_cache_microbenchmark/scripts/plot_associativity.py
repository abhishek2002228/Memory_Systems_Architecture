import sys
import pandas as pd
import matplotlib.pyplot as plt

if len(sys.argv) != 2:
    print("Usage: python plot_associativity.py cache_associativity.csv")
    sys.exit(1)

df = pd.read_csv(sys.argv[1])

plt.figure(figsize=(6,4))
plt.plot(df["ways"], df["avg_cycles"], marker="o")
plt.xlabel("Number of congruent lines accessed (ways)")
plt.ylabel("Average cycles (victim reload)")
plt.title("Cache Associativity Detection")
plt.grid(True)
plt.tight_layout()
plt.show()

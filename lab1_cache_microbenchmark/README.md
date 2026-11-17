# Lab 1 — Cache Hierarchy Microbenchmarks

Measures the cache hierarchy of an x86 CPU from user space: per-level capacity and
latency, cache line size, and associativity. Run on an Intel Xeon Gold 5117 @ 2.00 GHz.

## Results

| | L1 | L2 | L3 |
|---|---|---|---|
| Capacity | 32 KB | 1 MB | ~20 MB |
| Latency | 4–6 ns | 10–12 ns | 37–39 ns |
| Associativity | 8-way | 16-way | 8 (measured; documented 11, see report) |

Line size: **64 B**. DRAM latency: **~130 ns**.

## Method

| Experiment | Technique |
|---|---|
| Capacity / latency | Randomised pointer chase over growing working sets; each load depends on the last, so latency is exposed directly |
| Line size | `clflush` two addresses `Δ` apart, load the first, time the second with `rdtscp`; the knee in `Δ` is the line size |
| Associativity | Load a victim line, touch `N` lines that map to the same set, time the victim reload; the knee in `N` is the associativity |

## Layout

```
src/cache_microbenchmark.cpp   all experiments (writes CSVs)
data/                          raw measurements
scripts/plot_*.py              plots for each experiment
figures/                       plots used in the report
report/report.tex, report.pdf  write-up
```

## Running

```bash
g++ -O2 src/cache_microbenchmark.cpp -o cache_microbenchmark   # x86 only (uses x86intrin.h)
./cache_microbenchmark                                          # writes *.csv to the current directory
pip install pandas matplotlib
python scripts/plot_hierarchy.py data/cache_hierarchy_2.csv
python scripts/plot_line_size.py data/cache_line_size.csv
python scripts/plot_associativity.py data/cache_associativity.csv
```

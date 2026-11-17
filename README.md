# Memory System Architecture — Labs

| Lab | Topic | Highlights |
|---|---|---|
| [Lab 1](lab1_cache_microbenchmark/) | Cache hierarchy microbenchmarks | Measured L1/L2/L3 capacity (32 KB / 1 MB / ~20 MB), latency (4–6 / 10–12 / 37–39 ns, DRAM ~130 ns), 64 B lines and 8/16-way L1/L2 associativity on a Xeon Gold 5117 using pointer chasing, `clflush` and `rdtscp` |
| [Lab 2](lab2_crc2_cache_replacement/) | LLC replacement in ChampSim (CRC2) | Implemented SHIP++ and designed SHIP++ with Adaptive Prefetch Utility: 1.072× over LRU (1-core), 1.029× weighted speedup (4-core) |

Each lab folder has its own README, source, raw data and a LaTeX report (`report/report.tex`, compiled to `report.pdf`).

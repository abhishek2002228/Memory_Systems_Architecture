# Lab 2 — LLC Replacement: SHIP++ with Adaptive Prefetch Utility

Last-level-cache replacement policies for the CRC2 ChampSim framework.

- **Part A:** SHIP++ — [`replacement/ship_pp.llc_repl`](replacement/ship_pp.llc_repl) (paper: [SHiP++, Young et al., CRC-2 2017](docs/SHiP++.pdf))
- **Part B:** SHIP++ with Adaptive Prefetch Utility (APU) — [`replacement/custom.llc_repl`](replacement/custom.llc_repl)
## What my custom policy adds to SHIP++

1. **Adaptive Prefetch Utility.** Tracks the fraction of prefetched lines that get a demand hit
   before eviction, and makes prefetch insertion stricter when that fraction is low
   (<10%: always RRPV 3; 10–30%: RRPV 2 only if SHCT ≥ 6; ≥30%: RRPV 2 only if SHCT ≥ 5).
2. **Region-augmented prefetch signatures.** Prefetch signatures mix in a 6-bit tag of the
   4 KB physical region, so different streams from the same PC don't alias in the SHCT.
3. **Clean-first eviction.** Among RRPV-3 candidates, clean lines go first, reducing writebacks.

Storage: ≈25.7 KB per core. Full design and analysis: [`report/report.pdf`](report/report.pdf).

## Results (speedup over LRU, 10M warmup + 10M instructions)

| | SHIP++ | APU |
|---|---|---|
| 1-core, no prefetch | 1.0496 | 1.0520 |
| 1-core, next_line + ip_stride | 1.0527 | **1.0715** |
| 4-core weighted speedup (2 mixes) | 0.9972 | **1.0294** |

Raw outputs are in [`results/`](results/).

## Reproducing

```bash
# put bzip2_30M, libquantum_30M, mcf_30M, xalancbmk_30M .trace.gz files in traces/
./scripts/run_eval.sh          # builds binaries, runs 1-core and 4-core sets (JOBS=8 parallel)
python3 scripts/analyze.py     # prints the tables above
```

Single run:
```bash
./build_champsim.sh bimodal next_line ip_stride custom 1
./bin/bimodal-next_line-ip_stride-custom-1core -warmup_instructions 10000000 \
    -simulation_instructions 10000000 -traces traces/mcf_30M.trace.gz
```

## Replacement policy API

A policy is a single `replacement/<name>.llc_repl` file that starts with `#include "crc2_wrapper.h"`
and implements five functions. Build it with `./build_champsim.sh bimodal <l1d> <l2c> <name> <cores>`.

```cpp
void InitReplacementState();          // once at start: allocate RRPV/SHCT/etc.

// On an LLC miss: return the way to evict (0..LLC_WAY-1), or LLC_WAY to bypass.
uint32_t GetVictimInSet(uint32_t cpu, uint32_t set, const BLOCK *current_set,
                        uint64_t PC, uint64_t paddr, uint32_t type);

// On every LLC access, hit (hit=1) or fill (hit=0); victim_addr is 0 if nothing was evicted.
void UpdateReplacementState(uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr,
                            uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit);

void PrintStats_Heartbeat();          // every 1M instructions (optional)
void PrintStats();                    // end of simulation (optional)
```

- `type` is one of `LOAD`, `RFO`, `PREFETCH`, `WRITEBACK`.
- `BLOCK` exposes `valid`, `dirty`, `address`, `full_addr`, `tag`, `cpu` (don't use `lru`; the base simulator owns it).
- Helpers: `get_cycle_count()`, `get_instr_count(cpu)`, `get_config_number()`.

| LLC | 1-core | 4-core |
|---|---|---|
| Sets (`LLC_SET`) | 2048 | 8192 |
| Ways (`LLC_WAY`) | 16 | 16 |
| Size | 2 MB | 8 MB |
| Block | 64 B | 64 B |

Evaluated configurations: 1-core and 4-core, each with no prefetching (`no no`) or with
L1D `next_line` + L2C `ip_stride`.

## Layout

```
replacement/        policies: custom (APU), ship_pp, lru_crc2, srrip_crc2, drrip, template
results/            simulator outputs used in the report (1core/, 4core/)
scripts/            run_eval.sh, analyze.py, plus original ChampSim run scripts
report/             report.tex, report.pdf
docs/SHiP++.pdf     SHiP++ paper (Young, Jaleel, Chou, Qureshi; CRC-2 2017)
src/ inc/ branch/ prefetcher/ tracer/ sim_list/   ChampSim simulator
```

Changes to the provided framework: `build_champsim.sh` now edits the correct
`LOG2_DRAM_CHANNELS` macro for multi-core builds (it previously targeted a non-existent name,
leaving 2 DRAM channels with log2 = 0) and restores `lru_crc2` after building.

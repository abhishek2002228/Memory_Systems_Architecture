#include "crc2_wrapper.h"
#include <cassert>
#include <algorithm>

// ============================================================================
// CUSTOM3: SHIP++ with Adaptive Prefetch Utility (APU)
//
// Key insight: Prefetches are "guilty until proven innocent"
// - Track prefetch utility at runtime (prefetch→demand hit rate)
// - Adapt insertion policy based on how useful prefetches actually are
//
// APU Policy:
// - pf_utility < 10%: Ultra-skeptical, always RRPV=3 for prefetch
// - pf_utility < 30%: Skeptical, need SHCT>=6 for RRPV=2
// - pf_utility >= 30%: Trust prefetcher, need SHCT>=5 for RRPV=2
//
// Demand Policy: Standard SHIP++ (SHCT=0→3, SHCT=7→0, else→2)
// ============================================================================

// CONFIGURATION

#define maxRRPV 3              // 2-bit RRPV (0-3)
#define SHCT_SIZE 16384        // 16K entries (2^14)
#define SHCT_MAX 7             // 3-bit saturating counter (0-7)
#define NUM_SAMPLED_SETS 64    // Number of sampled sets for learning


// DATA STRUCTURES

// SHCT: Signature History Counter Table (per-core)
// 16K entries x 3-bit counters (0-7) per core
uint8_t SHCT[NUM_CPUS][SHCT_SIZE];

// RRPV: Re-Reference Prediction Value per cache line
uint8_t rrpv[LLC_SET][LLC_WAY];

// Per-line prefetch tracking
uint8_t line_is_prefetch[LLC_SET][LLC_WAY];
uint8_t line_got_demand_hit[LLC_SET][LLC_WAY];  // Did prefetch get useful demand hit?

// Adaptive Prefetch Utility (APU) counters
uint64_t apu_pf_fills = 0;          // Total prefetch fills
uint64_t apu_pf_useful = 0;         // Prefetches that got demand hit before eviction
uint64_t apu_demand_fills = 0;      // Total demand fills
uint64_t apu_demand_hits = 0;       // Demand that got reuse

// Compute current prefetch utility (0-100%)
inline uint32_t get_pf_utility() {
    if (apu_pf_fills < 1000) return 50;  // Not enough data, assume neutral
    return (100 * apu_pf_useful) / apu_pf_fills;
}

// Sampled set metadata (compact: only 64 sets)
uint16_t sampled_signatures[NUM_SAMPLED_SETS][LLC_WAY];
uint8_t sampled_reuse[NUM_SAMPLED_SETS][LLC_WAY];

// Maps set index -> sampled index (-1 if not sampled)
int16_t sampled_index[LLC_SET];

// HELPER FUNCTIONS

#ifndef REGION_SHIFT
#define REGION_SHIFT 12   // 4KB region
#endif

#ifndef REGION_BITS
#define REGION_BITS 6     // keep only 6 bits of region tag (prevents signature explosion)
#endif

inline uint16_t compute_signature(uint64_t PC, uint64_t paddr, bool is_pf) {
    // Base: PC-only signature (like your original, keeps training stable for demand)
    uint64_t sig = (PC << 1) | (uint64_t)is_pf;

    if (is_pf) {
        // For prefetch ONLY: mix in a COARSE region tag
        uint64_t region = (paddr >> REGION_SHIFT);
        uint64_t region_tag = region & ((1ULL << REGION_BITS) - 1ULL);

        // Light mixing (avoid heavy hashing; just break simple aliasing)
        sig ^= (region_tag << 2);
        sig ^= (region_tag * 1315423911ULL);  // cheap mixer
    }

    return (uint16_t)(sig % SHCT_SIZE);
}


// Select evenly-distributed sampled sets
void select_sampled_sets() {
    uint32_t interval = LLC_SET / NUM_SAMPLED_SETS;

    for (int i = 0; i < LLC_SET; i++) {
        sampled_index[i] = -1;
    }

    for (int i = 0; i < NUM_SAMPLED_SETS; i++) {
        uint32_t set = i * interval;
        sampled_index[set] = i;
    }
}

// ============================================================================
// REPLACEMENT POLICY API
// ============================================================================

void InitReplacementState() {
    cout << "Custom3: SHIP++ with Adaptive Prefetch Utility (APU)" << endl;
    cout << "  APU adapts prefetch insertion based on observed utility" << endl;
    cout << "  Demand: SHCT=0->3, SHCT=7->0, else->2" << endl;

    // Reset APU counters
    apu_pf_fills = 0;
    apu_pf_useful = 0;
    apu_demand_fills = 0;
    apu_demand_hits = 0;

    // Initialize SHCT to weak confidence (paper: start at 1)
    for (int c = 0; c < NUM_CPUS; c++) {
        for (int i = 0; i < SHCT_SIZE; i++) {
            SHCT[c][i] = 1;
        }
    }

    // Initialize RRPV to distant (3) and clear tracking bits
    for (int i = 0; i < LLC_SET; i++) {
        for (int j = 0; j < LLC_WAY; j++) {
            rrpv[i][j] = maxRRPV;
            line_is_prefetch[i][j] = 0;
            line_got_demand_hit[i][j] = 0;
        }
    }

    // Select and initialize sampled sets
    select_sampled_sets();
    for (int i = 0; i < NUM_SAMPLED_SETS; i++) {
        for (int j = 0; j < LLC_WAY; j++) {
            sampled_signatures[i][j] = 0;
            sampled_reuse[i][j] = 0;
        }
    }
}

uint32_t GetVictimInSet(uint32_t cpu, uint32_t set, const BLOCK *current_set,
                        uint64_t PC, uint64_t paddr, uint32_t type) {
    assert(set < (uint32_t)LLC_SET && "Invalid set index");

    int iterations = 0;
    while (1) {

        // Prefer a CLEAN victim among all RRPV==maxRRPV candidates
        int victim = -1;

        // First pass: try to find a clean line with RRPV==maxRRPV
        for (int way = 0; way < LLC_WAY; way++) {
            if (rrpv[set][way] == maxRRPV) {
                // NOTE: ChampSim BLOCK typically has a `dirty` field.
                // If your field name differs, change `dirty` accordingly.
                if (!current_set[way].dirty) {
                    victim = way;
                    break;
                }
                // fallback: remember the first maxRRPV candidate
                if (victim == -1) victim = way;
            }
        }

        // If we found any maxRRPV candidate, evict it (clean preferred)
        if (victim != -1) {
            int way = victim;
            assert(way < LLC_WAY && "Invalid way selected");

            // SHCT training on eviction: decrement if victim was never reused
            int sidx = sampled_index[set];
            if (sidx != -1) {
                assert(sidx >= 0 && sidx < NUM_SAMPLED_SETS && "Invalid sampled index");

                if (sampled_reuse[sidx][way] == 0) {
                    uint16_t sig = sampled_signatures[sidx][way];
                    uint32_t shct_idx = sig & 0x3FFF;
                    assert(sig < SHCT_SIZE && "Signature out of bounds");
                    if (SHCT[cpu][shct_idx] > 0) {
                        SHCT[cpu][shct_idx]--;
                    }
                }
            }

            // APU: Track prefetch utility on eviction
            if (line_is_prefetch[set][way]) {
                if (line_got_demand_hit[set][way]) {
                    apu_pf_useful++;  // This prefetch was useful!
                }
            }

            return (uint32_t)way;
        }

        // No victim with RRPV=3 found - age all entries
        iterations++;
        assert(iterations <= 4 && "RRPV aging not converging - infinite loop detected!");

        for (int way = 0; way < LLC_WAY; way++) {
            if (rrpv[set][way] < maxRRPV) {
                rrpv[set][way]++;
            }
        }
    }

    assert(0 && "GetVictimInSet: unreachable code");
    return 0;
}


void UpdateReplacementState(uint32_t cpu, uint32_t set, uint32_t way,
                            uint64_t paddr, uint64_t PC, uint64_t victim_addr,
                            uint32_t type, uint8_t hit) {
    assert(set < (uint32_t)LLC_SET && "Invalid set index");
    assert(way < (uint32_t)LLC_WAY && "Invalid way index");
    assert(type <= WRITEBACK && "Invalid access type");
    assert(rrpv[set][way] <= maxRRPV && "RRPV corrupted");

    int sidx = sampled_index[set];
    if (sidx != -1) {
        assert(sidx >= 0 && sidx < NUM_SAMPLED_SETS && "Invalid sampled index");
    }

    if (hit) {

        // Skip WRITEBACK hits - do nothing
        if (type == WRITEBACK) {
            return;
        }

        // Enhancement 5: Prefetch-Aware RRPV Updates
        if (type == PREFETCH && line_is_prefetch[set][way]) {
            // Prefetch hit on prefetched line: don't update RRPV
        }
        else {
            // Demand access or prefetch hit on demand line
            rrpv[set][way] = 0;

            if (line_is_prefetch[set][way]) {
                // Demand hit on prefetched line: demote to RRPV=3
                // But mark that this prefetch was USEFUL for APU tracking
                rrpv[set][way] = maxRRPV;
                line_got_demand_hit[set][way] = 1;  // APU: mark useful
                line_is_prefetch[set][way] = 0;
            }

            // Enhancement 2: Train SHCT only on first hit (reuse bit 0->1)
            if (sidx != -1 && sampled_reuse[sidx][way] == 0) {
                uint16_t sig = sampled_signatures[sidx][way];
                uint32_t shct_idx = sig & 0x3FFF;

                if (SHCT[cpu][shct_idx] < SHCT_MAX) {
                    SHCT[cpu][shct_idx]++;
                }
                sampled_reuse[sidx][way] = 1;
            }
        }

    } else {
        bool is_pf = (type == PREFETCH);

        if (type == WRITEBACK) {
            // Writeback: always insert at RRPV=3 (lowest priority)
            rrpv[set][way] = maxRRPV;
        } else {
            // Compute signature for SHCT lookup
            uint16_t sig = compute_signature(PC, paddr, is_pf);
            uint32_t shct_idx = sig & 0x3FFF;
            uint8_t shct_val = SHCT[cpu][shct_idx];

            if (is_pf) {
                // ============ PREFETCH: Adaptive based on utility ============
                // Get current prefetch utility rate
                uint32_t pf_util = get_pf_utility();
                apu_pf_fills++;  // Count this fill

                if (pf_util < 10) {
                    // Prefetcher is terrible (<10% useful)
                    // Ultra-skeptical: always lowest priority
                    rrpv[set][way] = maxRRPV;
                } else if (pf_util < 30) {
                    // Prefetcher is mediocre (10-30% useful)
                    // Only trust if SHCT is very high
                    if (shct_val >= 6) {
                        rrpv[set][way] = maxRRPV - 1;  // RRPV=2
                    } else {
                        rrpv[set][way] = maxRRPV;      // RRPV=3
                    }
                } else {
                    // Prefetcher is decent (>=30% useful)
                    // Trust if SHCT is reasonably high
                    if (shct_val >= 5) {
                        rrpv[set][way] = maxRRPV - 1;  // RRPV=2
                    } else {
                        rrpv[set][way] = maxRRPV;      // RRPV=3
                    }
                }
            } else {
                // ============ DEMAND (LOAD/RFO): Standard SHIP++ ============
                apu_demand_fills++;  // Count this fill
                if (shct_val == 0) {
                    // Dead-on-arrival prediction
                    rrpv[set][way] = maxRRPV;
                } else if (shct_val == SHCT_MAX) {
                    // High confidence: protect strongly
                    rrpv[set][way] = 0;
                } else {
                    // Default: moderate priority
                    rrpv[set][way] = maxRRPV - 1;
                }
            }

            // Store signature and reset reuse bit for sampled sets
            if (sidx != -1) {
                sampled_signatures[sidx][way] = sig;
                sampled_reuse[sidx][way] = 0;
            }
        }

        // Track whether line was brought by prefetch
        line_is_prefetch[set][way] = is_pf ? 1 : 0;
        line_got_demand_hit[set][way] = 0;  // Reset for new line
    }
}

void PrintStats_Heartbeat() {
}

void PrintStats() {
    cout << "\n=== Custom3 APU Statistics ===" << endl;
    cout << "Prefetch fills: " << apu_pf_fills << endl;
    cout << "Prefetch useful (got demand hit): " << apu_pf_useful << endl;
    if (apu_pf_fills > 0) {
        cout << "Prefetch utility rate: " << (100 * apu_pf_useful / apu_pf_fills) << "%" << endl;
    }
    cout << "Demand fills: " << apu_demand_fills << endl;
}
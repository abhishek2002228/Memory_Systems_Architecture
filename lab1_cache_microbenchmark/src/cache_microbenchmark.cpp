#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <x86intrin.h>

#define _POSIX_C_SOURCE 199309L


// Get current time in nanoseconds using CLOCK_MONOTONIC
double now_ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1.0e9 + t.tv_nsec;
}


// Store next index at offset 0 of each stride-sized slot.
// buf: base pointer to allocated memory
// array_size: total bytes available in buf
// stride: bytes per slot 
// returns: starting slot index
static uint32_t build_chain(uint8_t *buf, size_t array_size, size_t stride) {
    if (stride < sizeof(uint32_t)) stride = sizeof(uint32_t);

    size_t num_slots = array_size / stride;
    if (num_slots < 2) num_slots = 2;

    //avoid page faults during timing
    memset(buf, 0, num_slots * stride);

    // Create list of slot indices [0..num_slots-1]
    uint32_t *order = (uint32_t*)malloc(num_slots * sizeof(uint32_t));
    if (!order) return 0;

    for (uint32_t i = 0; i < (uint32_t)num_slots; i++) order[i] = i;

    // shuffle to randomize access order
    for (size_t i = num_slots - 1; i > 0; i--) {
        size_t j = (size_t)(rand() % (int)(i + 1));
        uint32_t tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }

    // Write "next" index for each slot, forming a circular list.
    // Slot address = buf + slot_index * stride
    for (size_t i = 0; i + 1 < num_slots; i++) {
        *(uint32_t*)(buf + (size_t)order[i] * stride) = order[i + 1];
    }
    *(uint32_t*)(buf + (size_t)order[num_slots - 1] * stride) = order[0];

    uint32_t start = order[0];
    free(order);
    return start;
}

double measure_access_time(size_t array_size, size_t stride, int iterations) {
    // Allocate a raw byte buffer, 64B-aligned
    uint8_t *buf = NULL;
    if (posix_memalign((void**)&buf, 64, array_size) != 0 || !buf) {
        return -1.0;
    }

    // Build randomized circular chain
    uint32_t idx = build_chain(buf, array_size, stride);

    // Warmup: walk a bit so caches/TLB are in steady state
    //volatile to prevent optimizations
    for (int i = 0; i < iterations / 10; i++) {
        volatile uint32_t *p = (volatile uint32_t*)(buf + (size_t)idx * stride);
        idx = *p;
    }

    // Timed pointer chasing: each iteration is a dependent load
    double start = now_ns();
    for (int i = 0; i < iterations; i++) {
        volatile uint32_t *p = (volatile uint32_t*)(buf + (size_t)idx * stride);
        idx = *p;
    }
    double end = now_ns();

    // Prevent compiler from optimizing away the loop
    if (idx == 0xFFFFFFFFu) fprintf(stderr, "\n");

    free(buf);

    // Average ns per dependent access
    return (end - start) / (double)iterations;
}

void measure_cache_line_size(const char *csv_name) {
    const size_t BUF_SZ = 8 * 1024 * 1024; // 8 MB buffer to avoid reuse effects
    const int TRIALS = 20000;
    const size_t MAX_DELTA = 256, STEP = 4;

    uint8_t *buf;
    posix_memalign((void**)&buf, 64, BUF_SZ); // cache-line aligned base
    memset(buf, 1, BUF_SZ);                  // touch pages to avoid page faults

    FILE *f = fopen(csv_name, "w");
    fprintf(f, "delta_bytes,avg_cycles\n");

    for (size_t delta = 0; delta <= MAX_DELTA; delta += STEP) {
        uint64_t sum = 0;

        for (int t = 0; t < TRIALS; t++) {
            // spread accesses across pages to avoid hitting same few lines
            size_t off = ((size_t)t * 4096) % (BUF_SZ - 8192);
            off = (off + 256) & ~((size_t)63); // align to cache line

            volatile uint8_t *p0 = buf + off;
            volatile uint8_t *p1 = p0 + delta;

            // force both lines out of cache
            _mm_clflush((const void*)p0);
            _mm_clflush((const void*)p1);
            _mm_mfence(); // ensure flush completes

            (void)*p0;   // bring exactly one cache line into cache

            // time access to p1
            _mm_lfence();
            unsigned aux;
            uint64_t t0 = __rdtscp(&aux);
            (void)*p1;
            uint64_t t1 = __rdtscp(&aux);
            _mm_lfence();

            sum += (t1 - t0);
        }

        fprintf(f, "%zu,%.2f\n", delta, (double)sum / TRIALS);
    }

    fclose(f);
    free(buf);
}


void experiment_cache_hierarchy() {
    const size_t stride = 64;   // guess for cache line size
    const int iterations = 1000000;

    fprintf(stderr, "Running Cache Hierarchy Detection...\n");  
    FILE* csv = fopen("cache_hierarchy.csv", "w");
    if (!csv) {
        perror("Failed to open CSV file");
        return;
    }

    // CSV header
    fprintf(csv, "Array_Size_KB,Latency_ns\n");

    // 1 KB to 128 KB (L1 region)
    for (size_t size = 1 * 1024; size <= 128 * 1024; size += 4 * 1024) {
        double latency = measure_access_time(size, stride, iterations);
        fprintf(csv, "%zu,%.3f\n", size / 1024, latency);
        fflush(csv);
    }

    // 128 KB to 2 MB (L2 region)
    for (size_t size = 128 * 1024; size <= 2 * 1024 * 1024; size += 64 * 1024) {
        double latency = measure_access_time(size, stride, iterations);
        fprintf(csv, "%zu,%.3f\n", size / 1024, latency);
        fflush(csv);
    }

    // 2 MB to 64 MB (L3 region)
    for (size_t size = 2 * 1024 * 1024; size <= 64 * 1024 * 1024; size += 1 * 1024 * 1024) {
        double latency = measure_access_time(size, stride, iterations);
        fprintf(csv, "%zu,%.3f\n", size / 1024, latency);
        fflush(csv);
    }

    // 64 MB to 512 MB 
    for (size_t size = 64 * 1024 * 1024; size <= 512 * 1024 * 1024; size += 16 * 1024 * 1024) {
        double latency = measure_access_time(size, stride, iterations);
        fprintf(csv, "%zu,%.3f\n", size / 1024, latency);
        fflush(csv);
    }

    // 512 MB to 1GB
    for (size_t size = 512 * 1024 * 1024; size <= 1 * 1024 * 1024 * 1024ULL; size += 64 * 1024 * 1024) {
        double latency = measure_access_time(size, stride, iterations);
        fprintf(csv, "%zu,%.3f\n", size / 1024, latency);
        fflush(csv);
    }

    fclose(csv);
  }

void measure_cache_associativity(const char *csv_name, size_t CACHE_SIZE) {
    const size_t LINE    = 64;          // 64B cache line
    const int TRIALS     = 20000;
    const int MAX_WAYS   = 24;          // sweep up to 24
    const size_t PAD     = 4096;        // avoid page-edge issues

    // Big enough to hold many set-conflicting addresses.
    const size_t BUF_SZ  = (MAX_WAYS + 2) * CACHE_SIZE + 2 * PAD;

    uint8_t *buf;
    posix_memalign((void**)&buf, 4096, BUF_SZ);
    memset(buf, 1, BUF_SZ);

    FILE *f = fopen(csv_name, "w");
    fprintf(f, "ways,avg_cycles\n");

    // Choose a base aligned to a cache line.
    volatile uint8_t *base = (volatile uint8_t *)(buf + PAD);
    uintptr_t b = ((uintptr_t)base + (LINE - 1)) & ~(uintptr_t)(LINE - 1);
    base = (volatile uint8_t*)b;

    // We will use same-set addresses by spacing them by CACHE_SIZE bytes.
    const size_t SAME_SET_STRIDE = CACHE_SIZE;

    for (int ways = 1; ways <= MAX_WAYS; ways++) {
        uint64_t sum = 0;

        for (int t = 0; t < TRIALS; t++) {
            volatile uint8_t *victim = base + (size_t)(t % 64) * LINE;

            // Warm victim into cache
            (void)*victim;

            // Touch 'ways' set-conflicting lines 
            for (int i = 1; i <= ways; i++) {
                volatile uint8_t *p = victim + (size_t)i * SAME_SET_STRIDE;
                (void)*p;
            }

            // Time victim reload (if evicted, this jumps)
            _mm_lfence();
            unsigned aux;
            uint64_t t0 = __rdtscp(&aux);
            (void)*victim;
            uint64_t t1 = __rdtscp(&aux);
            _mm_lfence();

            sum += (t1 - t0);
        }

        fprintf(f, "%d,%.2f\n", ways, (double)sum / TRIALS);
        fflush(f);
    }

    fclose(f);
    free(buf);
}

//--------------- Test Harness -----------------------

int main(void) {
  srand(1234);
  experiment_cache_hierarchy();
  measure_cache_line_size("cache_line_size.csv");
  measure_cache_associativity("cache_associativity.csv", 32 * 1024); // 32KB L1 cache
  measure_cache_associativity("l2_cache_associativity.csv", 1 * 1024 * 1024); // 1024KB L2 cache
  measure_cache_associativity("l3_cache_associativity.csv", 19712 * 1024); // 19712KB L3 cache
  return 0;
}

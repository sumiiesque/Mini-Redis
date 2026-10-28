#pragma once

#include <cstdint>
#include <vector>

namespace benchmark {

// Computes the 99th percentile of `latencies_us` (sorted in place). Uses
// the nearest-rank method: index = ceil(0.99 * n) - 1, clamped to
// [0, n-1] — the same method is used regardless of which backend produced
// the samples, so comparisons between backends are apples-to-apples.
// Returns 0 for an empty input.
uint64_t PercentileP99(std::vector<uint64_t>* latencies_us);

struct MemoryReading {
  bool ok = false;
  double resident_mb = 0.0;
};

// Reads the current process's resident set size from the VmRSS field of
// /proc/self/status (reported there in kB; converted to MB by dividing by
// 1024). Used identically for both backends so memory comparisons are
// apples-to-apples. If the field can't be read, `ok` is false — callers
// must treat this as a hard failure (per the project's requirement to fail
// explicitly rather than silently report zero), not substitute a default.
MemoryReading ReadProcessResidentMemory();

}  // namespace benchmark

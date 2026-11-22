#pragma once

#include <cstdint>
#include <string>

#include "benchmark/config.h"

namespace benchmark {

// Fixed schema: every field from the run's own configuration is included
// so each result is self-describing.
struct BenchmarkResult {
  BenchmarkConfig config;
  uint64_t operations = 0;
  double throughput_ops_sec = 0.0;
  uint64_t p99_latency_us = 0;
  double memory_mb = 0.0;
  uint64_t abort_count = 0;
  double abort_rate = 0.0;  // abort_count / operations; 0 for the locking backend (N/A -> 0)
};

// "backend,threads,read_percent,write_percent,keyspace,zipf_theta,seed,
//  warmup_seconds,duration_seconds,operations,throughput_ops_sec,
//  p99_latency_us,memory_mb,abort_count,abort_rate"
extern const char kCsvHeader[];

std::string FormatCsvRow(const BenchmarkResult& result);

// Appends one row to `path`, writing the header first if the file doesn't
// already exist (or is empty). Every invocation therefore emits exactly one
// complete, self-describing result row without disturbing prior rows —
// which is what lets the sweep driver call this repeatedly into the same
// file.
void AppendCsvRow(const std::string& path, const BenchmarkResult& result);

}  // namespace benchmark

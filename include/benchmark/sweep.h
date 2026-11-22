#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace benchmark {

struct ReadWriteRatio {
  int read_percent;
  int write_percent;
};

// Parameter ranges/lists to sweep. Every combination of
// backends x thread_counts x ratios x zipf_thetas is generated; the other
// fields are held fixed across the whole sweep so runs stay comparable.
struct SweepSpec {
  std::vector<std::string> backends;             // e.g. {"locking", "mvcc"}
  std::vector<int> thread_counts;                // e.g. {1, 4, 8}
  std::vector<ReadWriteRatio> read_write_ratios;  // e.g. {{100,0}, {50,50}}
  std::vector<double> zipf_thetas;                // e.g. {0.0, 0.99}
  int64_t keyspace = 10000;
  uint64_t seed = 42;
  double warmup_seconds = 1.0;
  double duration_seconds = 5.0;
  std::string output_csv = "results.csv";
};

// Generates the full command-line argument list (argv-style, one vector of
// tokens per invocation, NOT including argv[0]/the executable path itself)
// for every combination in `spec`. The same combinations are generated
// regardless of backend — i.e. for a fixed (threads, ratio, theta) point,
// every backend in spec.backends gets an identical set of flags apart from
// --backend itself — which is what keeps backend-to-backend comparisons
// meaningful. Order: backends is the outermost loop, then thread_counts,
// then read_write_ratios, then zipf_thetas (innermost).
std::vector<std::vector<std::string>> GenerateSweepCommands(const SweepSpec& spec);

}  // namespace benchmark

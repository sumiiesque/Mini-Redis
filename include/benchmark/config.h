#pragma once

#include <cstdint>
#include <string>

namespace benchmark {

struct BenchmarkConfig {
  std::string backend = "locking";  // "locking" or "mvcc"
  int threads = 4;
  int read_percent = 100;
  int write_percent = 0;
  int64_t keyspace = 10000;
  double zipf_theta = 0.99;
  uint64_t seed = 42;
  double warmup_seconds = 1.0;
  double duration_seconds = 5.0;
  std::string output_csv = "results.csv";
};

enum class ParseOutcome { kOk, kHelpRequested, kError };

struct ParsedArgs {
  ParseOutcome outcome = ParseOutcome::kError;
  BenchmarkConfig config;
  std::string error;  // set when outcome == kError
};

// Parses command-line flags of the form --name=value or --name value:
//   --backend locking|mvcc
//   --threads N
//   --read-percent N
//   --write-percent N
//   --keyspace N
//   --zipf-theta X
//   --seed N
//   --warmup-seconds X
//   --duration-seconds X
//   --output PATH
//   --help
// Unknown flags or malformed values produce ParseOutcome::kError with a
// human-readable message in `error`. Does not itself validate cross-field
// constraints (e.g. read+write == 100) — see ValidateConfig.
ParsedArgs ParseArgs(int argc, char** argv);

// Cross-field validation beyond what ParseArgs checks per-flag. Returns
// true and leaves `error` untouched if `cfg` is acceptable; otherwise
// returns false and fills `error` with the first violation found.
bool ValidateConfig(const BenchmarkConfig& cfg, std::string* error);

}  // namespace benchmark

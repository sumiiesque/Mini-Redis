#include <cstdio>

#include "benchmark/config.h"
#include "benchmark/result.h"
#include "benchmark/runner.h"

namespace {

void PrintUsage() {
  std::printf(
      "mini_redis_benchmark [flags]\n"
      "  --backend locking|mvcc   (default: locking)\n"
      "  --threads N              (default: 4)\n"
      "  --read-percent N         (default: 100)\n"
      "  --write-percent N        (default: 0; read-percent + write-percent must == 100)\n"
      "  --keyspace N             (default: 10000)\n"
      "  --zipf-theta X           (default: 0.99; 0 means uniform)\n"
      "  --seed N                 (default: 42)\n"
      "  --warmup-seconds X       (default: 1.0)\n"
      "  --duration-seconds X     (default: 5.0)\n"
      "  --output PATH            (default: results.csv)\n"
      "\n"
      "Example (locking):\n"
      "  mini_redis_benchmark --backend locking --threads 8 --read-percent 90 "
      "--write-percent 10 --keyspace 100000 --zipf-theta 0.99 --duration-seconds 10 "
      "--output results.csv\n"
      "\n"
      "Example (mvcc):\n"
      "  mini_redis_benchmark --backend mvcc --threads 8 --read-percent 90 "
      "--write-percent 10 --keyspace 100000 --zipf-theta 0.99 --duration-seconds 10 "
      "--output results.csv\n");
}

}  // namespace

int main(int argc, char** argv) {
  benchmark::ParsedArgs parsed = benchmark::ParseArgs(argc, argv);
  if (parsed.outcome == benchmark::ParseOutcome::kHelpRequested) {
    PrintUsage();
    return 0;
  }
  if (parsed.outcome == benchmark::ParseOutcome::kError) {
    std::fprintf(stderr, "error: %s\n", parsed.error.c_str());
    PrintUsage();
    return 1;
  }

  std::string validation_error;
  if (!benchmark::ValidateConfig(parsed.config, &validation_error)) {
    std::fprintf(stderr, "error: %s\n", validation_error.c_str());
    return 1;
  }

  try {
    benchmark::BenchmarkResult result = benchmark::RunBenchmark(parsed.config);
    benchmark::AppendCsvRow(parsed.config.output_csv, result);
    std::printf("%s\n", benchmark::FormatCsvRow(result).c_str());
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "benchmark run failed: %s\n", e.what());
    return 1;
  }
}

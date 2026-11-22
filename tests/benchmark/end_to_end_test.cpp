// Short end-to-end runs of the actual benchmark engine (RunBenchmark),
// in-process, against both backends — validating that the whole pipeline
// (correctness checks, pre-population, warmup, barrier-synchronized
// measurement, aggregation, CSV output) produces sane, complete rows
// without needing to spawn the built executable as a subprocess.

#include "benchmark/result.h"
#include "benchmark/runner.h"

#include <cstdio>
#include <fstream>
#include <unistd.h>

namespace {
int g_failures = 0;
int g_checks = 0;
}  // namespace

#define CHECK(cond)                                                                  \
  do {                                                                               \
    ++g_checks;                                                                      \
    if (!(cond)) {                                                                   \
      ++g_failures;                                                                  \
      std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
    }                                                                                \
  } while (0)

using benchmark::AppendCsvRow;
using benchmark::BenchmarkConfig;
using benchmark::BenchmarkResult;
using benchmark::RunBenchmark;

namespace {

std::string TempPath(const std::string& name) {
  return "/tmp/mini_redis_bench_e2e_" + name + "_" + std::to_string(::getpid()) + ".csv";
}

int CountLines(const std::string& path) {
  std::ifstream f(path);
  int n = 0;
  std::string line;
  while (std::getline(f, line)) ++n;
  return n;
}

void RunShortBenchmarkAndCheck(const std::string& backend, const std::string& csv_path) {
  BenchmarkConfig cfg;
  cfg.backend = backend;
  cfg.threads = 2;
  cfg.read_percent = 80;
  cfg.write_percent = 20;
  cfg.keyspace = 200;
  cfg.zipf_theta = 0.9;
  cfg.seed = 7;
  cfg.warmup_seconds = 0.05;
  cfg.duration_seconds = 0.2;
  cfg.output_csv = csv_path;

  BenchmarkResult result = RunBenchmark(cfg);
  CHECK(result.operations > 0);
  CHECK(result.throughput_ops_sec > 0.0);
  CHECK(result.memory_mb > 0.0);
  if (backend == "mvcc") {
    // Not asserting abort_count > 0 (contention-dependent with only 2
    // threads over 0.2s), just that it's a well-defined, non-negative stat.
    CHECK(result.abort_rate >= 0.0);
  } else {
    CHECK(result.abort_count == 0);
    CHECK(result.abort_rate == 0.0);
  }

  AppendCsvRow(csv_path, result);
}

}  // namespace

int main() {
  std::string csv_path = TempPath("main");
  ::unlink(csv_path.c_str());

  RunShortBenchmarkAndCheck("locking", csv_path);
  RunShortBenchmarkAndCheck("mvcc", csv_path);

  CHECK(CountLines(csv_path) == 3);  // header + 2 rows, one per backend

  ::unlink(csv_path.c_str());

  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}

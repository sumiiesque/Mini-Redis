#include "benchmark/result.h"

#include <cstdio>
#include <fstream>
#include <sstream>
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
using benchmark::BenchmarkResult;
using benchmark::FormatCsvRow;
using benchmark::kCsvHeader;

namespace {
std::string TempPath(const std::string& name) {
  return "/tmp/mini_redis_bench_result_test_" + name + "_" + std::to_string(::getpid()) + ".csv";
}

int CountLines(const std::string& path) {
  std::ifstream f(path);
  int n = 0;
  std::string line;
  while (std::getline(f, line)) ++n;
  return n;
}
}  // namespace

void TestHeaderHasExpectedColumns() {
  std::string header = kCsvHeader;
  CHECK(header.find("backend") != std::string::npos);
  CHECK(header.find("threads") != std::string::npos);
  CHECK(header.find("read_percent") != std::string::npos);
  CHECK(header.find("write_percent") != std::string::npos);
  CHECK(header.find("keyspace") != std::string::npos);
  CHECK(header.find("zipf_theta") != std::string::npos);
  CHECK(header.find("seed") != std::string::npos);
  CHECK(header.find("warmup_seconds") != std::string::npos);
  CHECK(header.find("duration_seconds") != std::string::npos);
  CHECK(header.find("operations") != std::string::npos);
  CHECK(header.find("throughput_ops_sec") != std::string::npos);
  CHECK(header.find("p99_latency_us") != std::string::npos);
  CHECK(header.find("memory_mb") != std::string::npos);
  CHECK(header.find("abort_count") != std::string::npos);
  CHECK(header.find("abort_rate") != std::string::npos);
}

void TestFormatRowFieldCount() {
  BenchmarkResult r;
  r.config.backend = "mvcc";
  r.operations = 12345;
  std::string row = FormatCsvRow(r);
  int commas = 0;
  for (char c : row) {
    if (c == ',') ++commas;
  }
  int header_commas = 0;
  for (char c : std::string(kCsvHeader)) {
    if (c == ',') ++header_commas;
  }
  CHECK(commas == header_commas);
}

void TestFormatRowContainsExpectedValues() {
  BenchmarkResult r;
  r.config.backend = "locking";
  r.config.threads = 8;
  r.operations = 999;
  std::string row = FormatCsvRow(r);
  CHECK(row.find("locking") != std::string::npos);
  CHECK(row.find("8") != std::string::npos);
  CHECK(row.find("999") != std::string::npos);
}

void TestAppendWritesHeaderOnceThenAppendsRows() {
  std::string path = TempPath("append");
  ::unlink(path.c_str());

  BenchmarkResult r1;
  r1.config.backend = "locking";
  AppendCsvRow(path, r1);
  CHECK(CountLines(path) == 2);  // header + 1 row

  BenchmarkResult r2;
  r2.config.backend = "mvcc";
  AppendCsvRow(path, r2);
  CHECK(CountLines(path) == 3);  // header + 2 rows (no duplicate header)

  ::unlink(path.c_str());
}

int main() {
  TestHeaderHasExpectedColumns();
  TestFormatRowFieldCount();
  TestFormatRowContainsExpectedValues();
  TestAppendWritesHeaderOnceThenAppendsRows();

  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}

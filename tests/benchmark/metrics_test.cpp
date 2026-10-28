#include "benchmark/metrics.h"

#include <cstdio>
#include <vector>

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

using benchmark::PercentileP99;
using benchmark::ReadProcessResidentMemory;

void TestP99OfEmptyIsZero() {
  std::vector<uint64_t> v;
  CHECK(PercentileP99(&v) == 0);
}

void TestP99OfHundredValues() {
  std::vector<uint64_t> v;
  for (uint64_t i = 1; i <= 100; ++i) v.push_back(i);  // 1..100
  // 99th percentile (nearest-rank) of 1..100 is 99.
  CHECK(PercentileP99(&v) == 99);
}

void TestP99IsOrderIndependent() {
  std::vector<uint64_t> v = {5, 1, 4, 100, 2, 3, 99, 98, 97, 6};
  uint64_t p99 = PercentileP99(&v);
  // n=10: idx = ceil(0.99*10) - 1 = 10 - 1 = 9 (0-indexed) -> the max, 100.
  CHECK(p99 == 100);
}

void TestP99SingleValue() {
  std::vector<uint64_t> v = {42};
  CHECK(PercentileP99(&v) == 42);
}

void TestP99AllIdentical() {
  std::vector<uint64_t> v(50, 7);
  CHECK(PercentileP99(&v) == 7);
}

void TestMemoryReadingSucceedsOnLinux() {
  auto reading = ReadProcessResidentMemory();
  CHECK(reading.ok);
  CHECK(reading.resident_mb > 0.0);
}

int main() {
  TestP99OfEmptyIsZero();
  TestP99OfHundredValues();
  TestP99IsOrderIndependent();
  TestP99SingleValue();
  TestP99AllIdentical();
  TestMemoryReadingSucceedsOnLinux();

  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}

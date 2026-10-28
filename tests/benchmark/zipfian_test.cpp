#include "benchmark/zipfian.h"

#include <algorithm>
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

using benchmark::ZipfianGenerator;

void TestSameSeedProducesIdenticalSequence() {
  ZipfianGenerator a(1000, 0.99, 42);
  ZipfianGenerator b(1000, 0.99, 42);
  bool all_match = true;
  for (int i = 0; i < 500; ++i) {
    if (a.Next() != b.Next()) all_match = false;
  }
  CHECK(all_match);
}

void TestDifferentSeedsLikelyDiffer() {
  ZipfianGenerator a(1000, 0.99, 1);
  ZipfianGenerator b(1000, 0.99, 2);
  int differences = 0;
  for (int i = 0; i < 200; ++i) {
    if (a.Next() != b.Next()) ++differences;
  }
  CHECK(differences > 0);
}

void TestAllSamplesInRange() {
  ZipfianGenerator z(500, 0.8, 7);
  bool ok = true;
  for (int i = 0; i < 5000; ++i) {
    int64_t v = z.Next();
    if (v < 0 || v >= 500) ok = false;
  }
  CHECK(ok);
}

void TestThetaZeroIsRoughlyUniform() {
  constexpr int64_t kN = 100;
  ZipfianGenerator z(kN, 0.0, 99);
  std::vector<int64_t> counts(kN, 0);
  constexpr int kSamples = 200000;
  for (int i = 0; i < kSamples; ++i) counts[z.Next()]++;

  // With a uniform distribution, no single bucket should dominate; loosely
  // check the max bucket isn't wildly above the expected average.
  int64_t max_count = 0;
  for (auto c : counts) max_count = std::max(max_count, c);
  double expected = static_cast<double>(kSamples) / static_cast<double>(kN);
  CHECK(static_cast<double>(max_count) < expected * 3.0);
}

void TestHighThetaIsSkewed() {
  constexpr int64_t kN = 100;
  ZipfianGenerator z(kN, 1.5, 99);
  std::vector<int64_t> counts(kN, 0);
  constexpr int kSamples = 50000;
  for (int i = 0; i < kSamples; ++i) counts[z.Next()]++;

  // Key 0 should be the overwhelmingly most popular under a strong skew.
  int64_t max_count = 0;
  int64_t max_key = -1;
  for (int64_t k = 0; k < kN; ++k) {
    if (counts[k] > max_count) {
      max_count = counts[k];
      max_key = k;
    }
  }
  CHECK(max_key == 0);
  CHECK(static_cast<double>(max_count) > static_cast<double>(kSamples) * 0.3);
}

void TestSingleKeyKeyspace() {
  ZipfianGenerator z(1, 0.99, 1);
  bool all_zero = true;
  for (int i = 0; i < 10; ++i) {
    if (z.Next() != 0) all_zero = false;
  }
  CHECK(all_zero);
}

int main() {
  TestSameSeedProducesIdenticalSequence();
  TestDifferentSeedsLikelyDiffer();
  TestAllSamplesInRange();
  TestThetaZeroIsRoughlyUniform();
  TestHighThetaIsSkewed();
  TestSingleKeyKeyspace();

  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}

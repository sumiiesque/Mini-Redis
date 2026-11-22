#pragma once

#include <cstdint>
#include <random>

namespace benchmark {

// Generates key indices in [0, n) following a Zipfian distribution with
// skew parameter theta, using the O(1)-per-sample algorithm from Gray et
// al., "Quickly Generating Billion-Record Synthetic Databases" (1994) (the
// same algorithm YCSB's ZipfianGenerator uses), after an O(n) one-time
// precomputation of the zeta function.
//
// theta == 0 is handled explicitly as the uniform-distribution case (the
// general algorithm is undefined there). Deterministic: the same (n, theta,
// seed) always produces the same sequence, and different generator
// instances never share RNG state — each worker in the benchmark
// constructs its own instance with a distinct, derived seed.
class ZipfianGenerator {
 public:
  ZipfianGenerator(int64_t n, double theta, uint64_t seed);

  int64_t Next();

 private:
  static double Zeta(int64_t n, double theta);

  int64_t n_;
  double theta_;
  double zetan_ = 0.0;
  double alpha_ = 0.0;
  double eta_ = 0.0;
  std::mt19937_64 rng_;
  std::uniform_real_distribution<double> uniform_{0.0, 1.0};
};

}  // namespace benchmark

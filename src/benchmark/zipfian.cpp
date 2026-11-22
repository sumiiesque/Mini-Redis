#include "benchmark/zipfian.h"

#include <algorithm>
#include <cmath>

namespace benchmark {

double ZipfianGenerator::Zeta(int64_t n, double theta) {
  double sum = 0.0;
  for (int64_t i = 1; i <= n; ++i) sum += 1.0 / std::pow(static_cast<double>(i), theta);
  return sum;
}

ZipfianGenerator::ZipfianGenerator(int64_t n, double theta, uint64_t seed)
    : n_(n), theta_(theta), rng_(seed) {
  if (theta_ > 0.0 && n_ > 1) {
    zetan_ = Zeta(n_, theta_);
    double zeta2 = Zeta(2, theta_);
    alpha_ = 1.0 / (1.0 - theta_);
    double pow_term = std::pow(2.0 / static_cast<double>(n_), 1.0 - theta_);
    eta_ = (1.0 - pow_term) / (1.0 - zeta2 / zetan_);
  }
}

int64_t ZipfianGenerator::Next() {
  double u = uniform_(rng_);

  if (theta_ <= 0.0 || n_ <= 1) {
    // Uniform-distribution case (also covers the degenerate n == 1 case).
    int64_t v = static_cast<int64_t>(u * static_cast<double>(n_));
    if (v >= n_) v = n_ - 1;
    if (v < 0) v = 0;
    return v;
  }

  double uz = u * zetan_;
  if (uz < 1.0) return 0;
  if (uz < 1.0 + std::pow(0.5, theta_)) return 1;

  int64_t v = static_cast<int64_t>(static_cast<double>(n_) *
                                    std::pow(eta_ * u - eta_ + 1.0, alpha_));
  return std::min<int64_t>(std::max<int64_t>(v, 0), n_ - 1);
}

}  // namespace benchmark

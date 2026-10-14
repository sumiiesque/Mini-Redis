#include "backends/locking_backend.h"

#include <chrono>

namespace backends {
namespace {

uint64_t Fnv1a64(const std::string& key) {
  uint64_t h = 14695981039346656037ULL;
  for (unsigned char c : key) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  return h;
}

}

LockingBackend::Stripe::Stripe() { ::pthread_rwlock_init(&lock, nullptr); }
LockingBackend::Stripe::~Stripe() { ::pthread_rwlock_destroy(&lock); }

LockingBackend::LockingBackend() {
  stripes_.reserve(kNumStripes);
  for (size_t i = 0; i < kNumStripes; ++i) stripes_.push_back(std::make_unique<Stripe>());
}

LockingBackend::~LockingBackend() = default;

size_t LockingBackend::StripeIndexFor(const std::string& key) {
  static_assert((kNumStripes & (kNumStripes - 1)) == 0, "kNumStripes must be a power of two");
  return static_cast<size_t>(Fnv1a64(key) & (kNumStripes - 1));
}

bool LockingBackend::IsExpired(const Entry& e, int64_t now_ms) {
  return e.expire_at_ms >= 0 && e.expire_at_ms <= now_ms;
}

int64_t LockingBackend::NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}

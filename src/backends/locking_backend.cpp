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

KVResult LockingBackend::Set(const std::string& key, const std::string& value,
                              int64_t expire_ms) {
  Stripe& s = *stripes_[StripeIndexFor(key)];
  ::pthread_rwlock_wrlock(&s.lock);
  Entry& e = s.map[key];
  e.value = value;
  e.expire_at_ms = expire_ms < 0 ? -1 : NowMs() + expire_ms;
  ::pthread_rwlock_unlock(&s.lock);
  return KVResult::Status(KVStatus::kSetOk);
}

KVResult LockingBackend::Get(const std::string& key) {
  Stripe& s = *stripes_[StripeIndexFor(key)];
  ::pthread_rwlock_rdlock(&s.lock);
  auto it = s.map.find(key);
  if (it == s.map.end() || IsExpired(it->second, NowMs())) {
    ::pthread_rwlock_unlock(&s.lock);
    return KVResult::Status(KVStatus::kGetNotFound);
  }
  KVResult r = KVResult::GetFound(it->second.value);
  ::pthread_rwlock_unlock(&s.lock);
  return r;
}

}

#pragma once

#include <cstdint>
#include <memory>
#include <pthread.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "kv_backend.h"

namespace backends {

constexpr size_t kNumStripes = 16384;

class LockingBackend : public KVBackend {
 public:
  LockingBackend();
  ~LockingBackend() override;

  LockingBackend(const LockingBackend&) = delete;
  LockingBackend& operator=(const LockingBackend&) = delete;

  KVResult Set(const std::string& key, const std::string& value,
               int64_t expire_ms = -1) override;
  KVResult Get(const std::string& key) override;
  KVResult Del(const std::string& key) override;
  KVResult Expire(const std::string& key, int64_t ttl_ms) override;

 private:
  struct Entry {
    std::string value;
    int64_t expire_at_ms = -1;
  };

  struct Stripe {
    Stripe();
    ~Stripe();
    Stripe(const Stripe&) = delete;
    Stripe& operator=(const Stripe&) = delete;

    pthread_rwlock_t lock;
    std::unordered_map<std::string, Entry> map;
  };

  static size_t StripeIndexFor(const std::string& key);
  static bool IsExpired(const Entry& e, int64_t now_ms);
  static int64_t NowMs();

  std::vector<std::unique_ptr<Stripe>> stripes_;
};

}  // namespace backends

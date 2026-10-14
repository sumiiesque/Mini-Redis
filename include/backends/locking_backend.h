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
};

}

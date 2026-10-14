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
};

}

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

}

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
}

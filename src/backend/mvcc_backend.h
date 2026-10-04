#include "backends/mvcc_backend.h"

#include <chrono>

namespace backends {

MVCCBackend::MVCCBackend() = default;

MVCCBackend::~MVCCBackend() {
  // Destructor: Free all live version chains in the directory shards,
  // then free any detached version nodes still waiting in the retirement buckets.
  for (auto& shard : directory_) {
    for (auto& [key, slot] : shard.map) {
      Version* cur = slot->head.load(std::memory_order_relaxed);
      while (cur) {
        Version* next = cur->prev;
        delete cur;
        cur = next;
      }
    }
  }
  for (size_t b = 0; b < kEpochCount; ++b) {
    for (Version* head : retired_[b]) {
      Version* cur = head;
      while (cur) {
        Version* next = cur->prev;
        delete cur;
        cur = next;
      }
    }
  }
}
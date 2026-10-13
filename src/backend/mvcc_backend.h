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

int64_t MVCCBackend::NowMs() {
  // Current time in milliseconds.
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

bool MVCCBackend::IsRealTimeExpired(const Version& v, int64_t now_ms) {
  // True if expiration was set and has already passed.
  return v.expire_at_ms >= 0 && v.expire_at_ms <= now_ms;
}

bool MVCCBackend::IsVisible(const Version* v, int64_t now_ms) {
  // A version is visible if it exists, is not a deletion tombstone, and is not expired.
  return v != nullptr && !v->tombstone && !IsRealTimeExpired(*v, now_ms);
}

size_t MVCCBackend::ShardIndexFor(const std::string& key) {
  // Hash the key using FNV-1a to pick one of 1,024 directory shards.
  static_assert((kDirectoryShards & (kDirectoryShards - 1)) == 0,
                "kDirectoryShards must be a power of two");
  uint64_t h = 14695981039346656037ULL;
  for (unsigned char c : key) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  return static_cast<size_t>(h & (kDirectoryShards - 1));
}

MVCCBackend::KeySlot* MVCCBackend::GetOrCreateSlot(const std::string& key) {
  // Find or create the KeySlot for this key in the appropriate shard.
  DirectoryShard& shard = directory_[ShardIndexFor(key)];
  std::lock_guard<std::mutex> lock(shard.mutex);
  auto it = shard.map.find(key);
  if (it != shard.map.end()) return it->second.get();

  auto slot = std::make_unique<KeySlot>();
  KeySlot* raw = slot.get();
  shard.map.emplace(key, std::move(slot));
  return raw;
}

MVCCBackend::KeySlot* MVCCBackend::FindSlot(const std::string& key) {
  // Lookup an existing KeySlot. Returns nullptr if the key was never accessed.
  DirectoryShard& shard = directory_[ShardIndexFor(key)];
  std::lock_guard<std::mutex> lock(shard.mutex);
  auto it = shard.map.find(key);
  return it == shard.map.end() ? nullptr : it->second.get();
}

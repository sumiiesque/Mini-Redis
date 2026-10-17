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

// ---------------------------------------------------------------------------
// Epoch-Based Reclamation (EBR) & Memory Safety Gate
//
// Problem: We can't delete old Version objects while other reader threads
// might still be reading them via 'prev' pointers.
//
// Solution: We use reclaim_gate_ (std::shared_mutex).
// - Normal reads and writes acquire it in SHARED mode (EnterEpoch/ExitEpoch).
// - Cleanup acquires it in EXCLUSIVE mode. If exclusive lock succeeds, we know
//   for certain that ZERO threads are currently reading any versions!
// ---------------------------------------------------------------------------

uint64_t MVCCBackend::EnterEpoch() {
  // Enter the reader safety gate.
  reclaim_gate_.lock_shared();
  return global_epoch_.load(std::memory_order_acquire);
}

void MVCCBackend::ExitEpoch() {
  // Exit the reader safety gate.
  reclaim_gate_.unlock_shared();
}

void MVCCBackend::Retire(Version* detached_chain_head, uint64_t epoch) {
  // Put detached old versions into the retirement list for this epoch.
  size_t bucket = epoch % kEpochCount;
  {
    std::lock_guard<std::mutex> lock(retired_mutex_[bucket]);
    retired_[bucket].push_back(detached_chain_head);
  }
  reclaim_work_pending_.store(true, std::memory_order_relaxed);
}

void MVCCBackend::ReclaimBucket(size_t bucket) {
  // Free all Version nodes waiting in this retirement bucket.
  std::vector<Version*> to_free;
  {
    std::lock_guard<std::mutex> lock(retired_mutex_[bucket]);
    to_free.swap(retired_[bucket]);
  }
  for (Version* head : to_free) {
    Version* cur = head;
    while (cur) {
      Version* next = cur->prev;
      delete cur;
      cur = next;
    }
  }
}

bool MVCCBackend::NotePublishedVersion(KeySlot* slot) {
  // Increment chain length. If it exceeds kMaxChainDepth (32), schedule for trimming.
  uint32_t length = slot->chain_length.fetch_add(1, std::memory_order_relaxed) + 1;
  if (length >= kMaxChainDepth && !slot->trim_queued.exchange(true, std::memory_order_acq_rel)) {
    {
      std::lock_guard<std::mutex> lock(trim_mutex_);
      trim_candidates_.push_back(slot);
    }
    reclaim_work_pending_.store(true, std::memory_order_relaxed);
  }
  // If chain got dangerously long (128), force a blocking cleanup.
  return length >= kHardChainCap;
}

void MVCCBackend::TrimQueuedChainsLocked() {
  // Cut chains that grew past kMaxChainDepth (32) and send old tails to Retire().
  // Caller must hold reclaim_gate_ exclusively.
  std::vector<KeySlot*> candidates;
  {
    std::lock_guard<std::mutex> lock(trim_mutex_);
    candidates.swap(trim_candidates_);
  }

  const uint64_t epoch = global_epoch_.load(std::memory_order_acquire);
  for (KeySlot* slot : candidates) {
    slot->trim_queued.store(false, std::memory_order_release);

    Version* node = slot->head.load(std::memory_order_acquire);
    if (node == nullptr) {
      slot->chain_length.store(0, std::memory_order_relaxed);
      continue;
    }

    // Keep the most recent 32 versions.
    uint32_t retained = 1;
    while (retained < kMaxChainDepth && node->prev != nullptr) {
      node = node->prev;
      ++retained;
    }

    // Detach anything older than 32 versions.
    if (node->prev != nullptr) {
      Version* tail = node->prev;
      node->prev = nullptr;
      Retire(tail, epoch);
    }
    slot->chain_length.store(retained, std::memory_order_relaxed);
  }
}

void MVCCBackend::ReclaimAndTrimLocked() {
  // Truncate long chains, free retired versions, and advance epoch counter.
  reclaim_work_pending_.store(false, std::memory_order_relaxed);
  TrimQueuedChainsLocked();
  for (size_t b = 0; b < kEpochCount; ++b) ReclaimBucket(b);
  global_epoch_.fetch_add(1, std::memory_order_relaxed);
}

void MVCCBackend::TryAdvanceEpoch() {
  // Non-blocking cleanup check. We only check once every 256 ops to keep CPU cache fast.
  static thread_local uint32_t ops_since_attempt = 0;
  if (++ops_since_attempt < kReclaimAttemptInterval) return;
  ops_since_attempt = 0;

  if (!reclaim_work_pending_.load(std::memory_order_relaxed)) return;

  // Try to acquire the gate exclusively without waiting.
  // If any reader is currently active, try_lock() fails and we simply try next time.
  if (!reclaim_gate_.try_lock()) return;
  ReclaimAndTrimLocked();
  reclaim_gate_.unlock();
}

void MVCCBackend::ForceAdvanceEpoch() {
  // Blocking cleanup: only called when version chains hit the hard cap (128).
  std::lock_guard<std::shared_mutex> lock(reclaim_gate_);
  ReclaimAndTrimLocked();
}

void MVCCBackend::FinishEpoch(bool needs_forced_reclaim) {
  if (needs_forced_reclaim) {
    ForceAdvanceEpoch();
  } else {
    TryAdvanceEpoch();
  }
}

// ---------------------------------------------------------------------------
// KVBackend Operations: Set, Get, Del, Expire
// ---------------------------------------------------------------------------

template <typename MakeVersionFn>
bool MVCCBackend::PublishVersion(KeySlot* slot, MakeVersionFn&& make_version,
                                 bool* needs_forced_reclaim) {
  // Atomic CAS loop:
  // 1. Load the current head of the version chain.
  // 2. Build our proposed new Version pointing back to old_head.
  // 3. Atomically swap head from old_head -> new_version.
  // 4. If someone else changed head before us, CAS fails: retry with new head!
  *needs_forced_reclaim = false;
  for (;;) {
    Version* old_head = slot->head.load(std::memory_order_acquire);
    std::unique_ptr<Version> nv = make_version(old_head);
    if (!nv) {
      return false;  // Condition failed (e.g. key doesn't exist or is expired)
    }

    nv->prev = old_head;
    nv->version_id = version_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;

    Version* raw = nv.get();
    if (slot->head.compare_exchange_strong(old_head, raw, std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
      nv.release();  // CAS won: transfer ownership to the chain
      *needs_forced_reclaim = NotePublishedVersion(slot);
      return true;
    }

    // CAS lost race: someone wrote first. std::unique_ptr frees nv, and we retry.
    abort_count_.fetch_add(1, std::memory_order_relaxed);
  }
}

KVResult MVCCBackend::Set(const std::string& key, const std::string& value, int64_t expire_ms) {
  // 1. Get or create the KeySlot for this key.
  KeySlot* slot = GetOrCreateSlot(key);

  // 2. Protect with shared epoch gate.
  EnterEpoch();

  // 3. Publish a new Version node with the value and expiration.
  bool needs_forced_reclaim = false;
  PublishVersion(
      slot,
      [&](Version* /*old_head*/) {
        auto nv = std::make_unique<Version>();
        nv->value = value;
        nv->tombstone = false;
        nv->expire_at_ms = (expire_ms < 0) ? -1 : NowMs() + expire_ms;
        return nv;
      },
      &needs_forced_reclaim);

  // 4. Leave epoch gate and run cleanup if needed.
  ExitEpoch();
  FinishEpoch(needs_forced_reclaim);
  return KVResult::Status(KVStatus::kSetOk);
}

KVResult MVCCBackend::Get(const std::string& key) {
  KeySlot* slot = FindSlot(key);
  if (slot == nullptr) {
    TryAdvanceEpoch();
    return KVResult::Status(KVStatus::kGetNotFound);
  }

  EnterEpoch();

  // Step 1: Capture snapshot version timestamp.
  uint64_t snapshot = version_counter_.load(std::memory_order_acquire);

  // Step 2: Walk backward along the chain until we find a version <= our snapshot.
  Version* v = slot->head.load(std::memory_order_acquire);
  while (v != nullptr && v->version_id > snapshot) {
    v = v->prev;
  }

  // Step 3: Check if that version is visible (not deleted or expired).
  KVResult result;
  if (!IsVisible(v, NowMs())) {
    result = KVResult::Status(KVStatus::kGetNotFound);
  } else {
    result = KVResult::GetFound(v->value);
  }

  ExitEpoch();
  TryAdvanceEpoch();
  return result;
}

KVResult MVCCBackend::Del(const std::string& key) {
  KeySlot* slot = FindSlot(key);
  if (slot == nullptr) {
    TryAdvanceEpoch();
    return KVResult::Status(KVStatus::kDelNotFound);
  }

  EnterEpoch();

  // To delete in MVCC, we publish a new version with tombstone = true.
  bool needs_forced_reclaim = false;
  bool deleted = PublishVersion(
      slot,
      [&](Version* old_head) -> std::unique_ptr<Version> {
        if (!IsVisible(old_head, NowMs())) return nullptr;
        auto nv = std::make_unique<Version>();
        nv->tombstone = true;
        return nv;
      },
      &needs_forced_reclaim);

  ExitEpoch();
  FinishEpoch(needs_forced_reclaim);
  return KVResult::Status(deleted ? KVStatus::kDelOk : KVStatus::kDelNotFound);
}

KVResult MVCCBackend::Expire(const std::string& key, int64_t ttl_ms) {
  KeySlot* slot = FindSlot(key);
  if (slot == nullptr) {
    TryAdvanceEpoch();
    return KVResult::Status(KVStatus::kExpireNotFound);
  }

  EnterEpoch();

  // Create a new version with the same value but an updated expiration timestamp.
  bool needs_forced_reclaim = false;
  bool updated = PublishVersion(
      slot,
      [&](Version* old_head) -> std::unique_ptr<Version> {
        if (!IsVisible(old_head, NowMs())) return nullptr;
        auto nv = std::make_unique<Version>();
        nv->value = old_head->value;  // Keep same value
        nv->tombstone = false;
        nv->expire_at_ms = (ttl_ms < 0) ? -1 : NowMs() + ttl_ms;
        return nv;
      },
      &needs_forced_reclaim);

  ExitEpoch();
  FinishEpoch(needs_forced_reclaim);
  return KVResult::Status(updated ? KVStatus::kExpireOk : KVStatus::kExpireNotFound);
}

}  // namespace backends

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "kv_backend.h"

namespace backends {

class MVCCBackend : public KVBackend {
 public:
  MVCCBackend();
  ~MVCCBackend() override;

  MVCCBackend(const MVCCBackend&) = delete;
  MVCCBackend& operator=(const MVCCBackend&) = delete;

  KVResult Set(const std::string& key, const std::string& value,
               int64_t expire_ms = -1) override;
  KVResult Get(const std::string& key) override;
  KVResult Del(const std::string& key) override;
  KVResult Expire(const std::string& key, int64_t ttl_ms) override;

 private:
  struct Version {
    std::string value;
    bool tombstone = false;
    int64_t expire_at_ms = -1;
    uint64_t version_id = 0;
    Version* prev = nullptr;
  };

  struct KeySlot {
    std::atomic<Version*> head{nullptr};
    std::atomic<uint32_t> chain_length{0};
    std::atomic<bool> trim_queued{false};
  };

  struct DirectoryShard {
    std::mutex mutex;
    std::unordered_map<std::string, std::unique_ptr<KeySlot>> map;
  };

  using MakeVersionFn =
      std::function<std::unique_ptr<Version>(Version*)>;

  static constexpr size_t kDirectoryShards = 1024;
  static constexpr size_t kEpochCount = 3;
  static constexpr uint32_t kMaxChainDepth = 32;
  static constexpr uint32_t kHardChainCap = 128;
  static constexpr uint32_t kReclaimAttemptInterval = 256;

  static int64_t NowMs();
  static bool IsRealTimeExpired(const Version& v, int64_t now_ms);
  static bool IsVisible(const Version* v, int64_t now_ms);
  static size_t ShardIndexFor(const std::string& key);

  KeySlot* GetOrCreateSlot(const std::string& key);
  KeySlot* FindSlot(const std::string& key);

  uint64_t EnterEpoch();
  void ExitEpoch();
  void Retire(Version* detached_chain_head, uint64_t epoch);
  void ReclaimBucket(size_t bucket);
  bool NotePublishedVersion(KeySlot* slot);
  void TrimQueuedChainsLocked();
  void ReclaimAndTrimLocked();
  void TryAdvanceEpoch();
  void ForceAdvanceEpoch();
  void FinishEpoch(bool needs_forced_reclaim);

  template <typename MakeVersionFnType>
  bool PublishVersion(KeySlot* slot, MakeVersionFnType&& make_version,
                      bool* needs_forced_reclaim);

  std::array<DirectoryShard, kDirectoryShards> directory_{};

  std::atomic<uint64_t> global_epoch_{0};
  std::atomic<uint64_t> version_counter_{0};
  std::atomic<uint64_t> abort_count_{0};

  std::shared_mutex reclaim_gate_;
  std::atomic<bool> reclaim_work_pending_{false};

  std::array<std::vector<Version*>, kEpochCount> retired_{};
  std::array<std::mutex, kEpochCount> retired_mutex_{};

  std::mutex trim_mutex_;
  std::vector<KeySlot*> trim_candidates_;
};

}  // namespace backends

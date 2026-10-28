#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "kv_backend.h"

namespace replication {

// Replica side of replication. Connects to a primary and applies the
// mutation stream it receives to `backend` through the ordinary KVBackend
// interface. There is no distinct "bootstrap phase" at the protocol level:
// the primary sends the AOF-snapshot bytes followed seamlessly by live
// mutation records in the same uniform wire format (see
// ReplicationPrimary), so this class just decodes and applies records in
// whatever order they arrive, forever, for as long as the connection lasts.
//
// On disconnect (primary shutdown, network error, malformed stream) this
// reconnects with a short backoff and starts over from a fresh bootstrap.
// Replaying the full AOF history again on reconnect is safe even though the
// backend may already reflect some or all of it: SET/DEL/EXPIRE replay is
// idempotent when applied in recorded order (each record's effect is fully
// determined by the record itself, not by whatever state preceded it), so
// state after a reconnect-triggered full replay converges to exactly the
// same result as uninterrupted live replication would have produced.
class ReplicationReplica {
 public:
  ReplicationReplica(KVBackend& backend, std::string primary_host, int primary_port);
  ~ReplicationReplica();

  ReplicationReplica(const ReplicationReplica&) = delete;
  ReplicationReplica& operator=(const ReplicationReplica&) = delete;

  // Starts the connect/apply/reconnect loop on a background thread.
  void Start();

  // Signals the loop to stop and disconnects. Safe to call once; the
  // destructor calls it if not already called.
  void Stop();

  // Total records successfully applied since Start() (across all
  // connections/reconnects) — useful for tests/observability.
  uint64_t AppliedCount() const { return applied_count_.load(std::memory_order_relaxed); }

  bool IsConnected() const { return connected_.load(std::memory_order_relaxed); }

 private:
  void RunLoop();
  // Connects, streams+applies until disconnected/malformed/stopped. Returns
  // so RunLoop can decide whether to back off and reconnect.
  void ConnectAndApplyUntilDisconnected();

  KVBackend& backend_;
  std::string host_;
  int port_;

  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> connected_{false};
  std::atomic<uint64_t> applied_count_{0};
  std::thread thread_;
  std::atomic<int> fd_{-1};  // written by the run-loop thread, read by Stop() from another thread
};

}  // namespace replication

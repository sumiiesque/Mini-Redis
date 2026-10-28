#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "persistence/aof_writer.h"
#include "persistence/mutation_record.h"

namespace replication {

// Primary side of replication. Accepts replica TCP connections (thread per
// connection — replication links are few and long-lived, unlike the
// client-facing request path, so this is a deliberately different, simpler
// design than Phase 1's fixed worker pool). Each mutation the caller wants
// replicated is recorded via RecordAndBroadcast(), which:
//   1. appends it to the AOF (through the given AofWriter),
//   2. enqueues the same framed bytes for every currently-connected replica.
//
// A newly-connecting replica is bootstrapped by streaming the AOF file's
// current bytes to it — which, since the AOF wire format and the
// replication wire format are identical (both are sequences of
// SerializeMutationRecord() output), is just a raw file copy — and is only
// registered into the live-broadcast list afterward. Bootstrapping and
// broadcasting both hold the same mutex for their full duration, so a
// mutation is delivered to a given replica exactly once: either it is
// already in the bootstrap snapshot (mutation happened before this replica
// connected) or it arrives live (mutation happens after), never both and
// never neither.
class ReplicationPrimary {
 public:
  ReplicationPrimary(std::string bind_address, int port, std::string aof_path);
  ~ReplicationPrimary();

  ReplicationPrimary(const ReplicationPrimary&) = delete;
  ReplicationPrimary& operator=(const ReplicationPrimary&) = delete;

  // Starts the accept loop on a background thread. Throws std::runtime_error
  // on bind/listen failure.
  void Start();

  // Stops accepting new replicas and disconnects existing ones. Safe to
  // call once; the destructor calls it if not already called.
  void Stop();

  // Appends `record` to `aof` and enqueues it for every connected replica,
  // as one atomic step with respect to new replicas bootstrapping (see
  // class comment).
  void RecordAndBroadcast(persistence::AofWriter& aof, const persistence::MutationRecord& record);

  // 0 until Start() has bound the listening socket.
  int ListenPort() const { return actual_port_.load(std::memory_order_acquire); }

  size_t ReplicaCount() const;

 private:
  struct Replica {
    int fd;
    std::thread sender_thread;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::deque<std::string> queue;
    bool stop = false;
    std::atomic<bool> alive{true};  // set false by SenderLoop itself on failure/stop
  };

  void AcceptLoop();
  void SenderLoop(const std::shared_ptr<Replica>& replica);
  void BootstrapAndRegister(int client_fd);
  // Removes any replica whose sender thread has already exited (alive ==
  // false), joining it first. Only ever called from the accept-loop thread
  // or from Stop()/the destructor — never from a sender thread itself,
  // since a thread cannot safely join itself.
  void ReapDeadReplicas();

  std::string bind_address_;
  int port_;
  std::string aof_path_;

  std::atomic<int> listen_fd_{-1};  // written by Start()/Stop(), read by AcceptLoop's own thread
  std::atomic<int> actual_port_{0};
  std::atomic<bool> stop_requested_{false};
  std::thread accept_thread_;

  mutable std::mutex replicas_mutex_;  // also guards RecordAndBroadcast <-> bootstrap ordering
  std::vector<std::shared_ptr<Replica>> replicas_;
};

}  // namespace replication

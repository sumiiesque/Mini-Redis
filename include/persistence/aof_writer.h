#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace persistence {

// Durability mode for AofWriter. In both modes every accepted mutation is
// appended (via write()) in a well-defined, strictly increasing order —
// they differ only in when the bytes are guaranteed to survive a crash.
enum class DurabilityMode {
  // Bytes are handed to the OS (write()) but not forced to disk after every
  // record. Durable against process crashes once the OS has scheduled the
  // write-back, but NOT durable against an OS/power failure that occurs
  // before that write-back happens.
  kBuffered,
  // Every record is followed by fsync(), so the call does not return until
  // the record is durable on disk. Durable against process AND OS/power
  // failure, at the cost of a syscall (and disk flush) per mutation.
  kSyncEveryWrite,
};

// Append-only file writer for MutationRecord entries. Thread-safe: multiple
// worker threads may call Append* concurrently, and records are still
// appended in a well-defined total order (guarded by an internal mutex —
// appropriate here since durable persistence is inherently serialized by
// the underlying file anyway).
class AofWriter {
 public:
  AofWriter(const std::string& path, DurabilityMode mode);
  ~AofWriter();

  AofWriter(const AofWriter&) = delete;
  AofWriter& operator=(const AofWriter&) = delete;

  // expire_at_ms_or_none: -1 means no expiration; otherwise an ABSOLUTE
  // epoch-ms timestamp (the caller is responsible for the relative-TTL ->
  // absolute-timestamp conversion, matching KVBackend's own SET contract).
  void AppendSet(const std::string& key, const std::string& value, int64_t expire_at_ms_or_none);
  void AppendDel(const std::string& key);
  // ttl absolute expiry, matching KVBackend::Expire's semantics but logged
  // as an absolute timestamp per the project's persistence contract.
  void AppendExpire(const std::string& key, int64_t expire_at_ms);

  // Forces any OS-buffered bytes to disk, regardless of durability mode.
  void Sync();

 private:
  void WriteFramed(const std::string& framed);

  std::mutex mutex_;
  int fd_ = -1;
  DurabilityMode mode_;
};

}  // namespace persistence

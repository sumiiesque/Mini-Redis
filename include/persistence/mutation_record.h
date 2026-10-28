#pragma once

#include <cstdint>
#include <string>

#include "kv_backend.h"

namespace persistence {

// A single logged mutation. GET is never represented here — only operations
// that change backend state are logged/replicated.
//
// Expiration is always logged as an ABSOLUTE timestamp (milliseconds since
// epoch), never a relative TTL: this is what lets replay, after any amount
// of elapsed wall-clock time, reconstruct the correct expiration moment
// instead of restarting a fresh countdown from the replay time. A key whose
// logged absolute expiration has already passed by the time it is replayed
// is expected to come back expired/missing — that is correct behavior, not
// a bug.
enum class MutationType : uint8_t {
  kSet = 1,
  kDel = 2,
  kExpire = 3,
};

struct MutationRecord {
  MutationType type = MutationType::kSet;
  std::string key;
  std::string value;          // meaningful only for kSet
  bool has_expire = false;    // meaningful only for kSet (kExpire always has it)
  int64_t expire_at_ms = -1;  // absolute epoch ms; meaningful when has_expire (kSet) or always (kExpire)
};

// Encodes `r` as one length-delimited, binary-safe record:
//   [uint32_t total_len (little-endian)] [uint8_t type] [type-specific payload]
// `total_len` covers everything after itself (type + payload), so a reader
// can always tell, from just the first 4 bytes, exactly how many more bytes
// this record needs — which is what makes truncation detection trivial and
// keys/values containing arbitrary bytes (including embedded NULs, newlines,
// or bytes that look like framing) safe to store without ambiguity.
std::string SerializeMutationRecord(const MutationRecord& r);

enum class DecodeStatus {
  kOk,          // a full record was decoded
  kIncomplete,  // not enough buffered bytes yet; feed more and retry
  kMalformed,   // corrupt: bad type, a length field that overruns the
                // record, or a declared size beyond the sanity limit
};

// Maximum encoded size of a single record body (type + payload), guarding
// against a corrupt length field causing an unbounded allocation.
constexpr uint32_t kMaxRecordBodyBytes = 8u * 1024u * 1024u;  // 8 MiB

// Incremental decoder mirroring the shape of net::RespParser: Feed()
// arbitrarily-fragmented chunks (a file read or a socket read), then call
// TryDecode() repeatedly to drain every fully-buffered record. Used
// identically for AOF file replay and for the replication TCP stream, so
// both get the same truncation/malformed-record handling for free.
class MutationStreamDecoder {
 public:
  void Feed(const char* data, size_t len);

  // - kOk: `*out` is populated and the consumed bytes are removed from the
  //   internal buffer; call again to check for another buffered record.
  // - kIncomplete: buffer unchanged; the caller is expected to Feed() more
  //   data (or, at EOF, treat this as a truncated final record — not an
  //   error).
  // - kMalformed: buffer unchanged; the stream must not be used further.
  DecodeStatus TryDecode(MutationRecord* out);

  // True if any bytes remain buffered (used to distinguish "clean EOF
  // between records" from "EOF mid-record").
  bool HasBufferedBytes() const { return !buf_.empty(); }

 private:
  std::string buf_;
};

// Applies one decoded record to `backend` through the ordinary KVBackend
// interface, converting any absolute expiration timestamp in the record to
// a relative expire_ms/ttl_ms "as of `now_ms`" — the same conversion rule
// used by both AOF recovery and live replica application, which is exactly
// why this lives here instead of being duplicated in each caller: an
// already-past absolute timestamp maps to 0 (immediate expiration, per
// KVBackend's documented TTL-zero behavior), never to a negative number
// (KVBackend reserves -1 specifically for "no expiration").
void ApplyMutationRecord(KVBackend& backend, const MutationRecord& r, int64_t now_ms);

}  // namespace persistence

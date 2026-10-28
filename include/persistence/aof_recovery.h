#pragma once

#include <cstdint>
#include <string>

#include "kv_backend.h"

namespace persistence {

struct RecoveryStats {
  uint64_t records_applied = 0;
  bool truncated_final_record = false;  // benign: last record was incomplete, safely ignored
  bool malformed_record_found = false;  // replay stopped early due to corruption
  bool file_existed = false;
};

// Replays an AOF file into `backend` sequentially, in log order, through
// the ordinary KVBackend interface (never touching backend internals —
// this works identically for any KVBackend implementation).
//
// - A missing file is not an error (fresh start): returns file_existed =
//   false, records_applied = 0.
// - A truncated final record (the file ends mid-record) is not an error:
//   replay stops there and truncated_final_record is set.
// - A malformed record (bad type, or a length field that doesn't fit the
//   declared record bounds) is rejected without being applied; replay stops
//   at that point and malformed_record_found is set, so no undefined data
//   is ever applied to the backend.
// - Every SET/EXPIRE record's absolute expiration timestamp is converted
//   back to a relative TTL as of "now" (at replay time). If that timestamp
//   has already passed, the key is replayed with an immediate (TTL == 0)
//   expiration, per KVBackend's contract — ending up expired/missing after
//   replay, which is correct, not a bug.
RecoveryStats RecoverFromFile(const std::string& path, KVBackend& backend);

}  // namespace persistence

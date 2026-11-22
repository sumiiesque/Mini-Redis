#include "persistence/aof_recovery.h"

#include <chrono>
#include <cstdio>

#include "persistence/mutation_record.h"

namespace persistence {
namespace {

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

RecoveryStats RecoverFromFile(const std::string& path, KVBackend& backend) {
  RecoveryStats stats;

  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return stats;  // fresh start: no AOF yet, not an error
  stats.file_existed = true;

  const int64_t now_ms = NowMs();
  MutationStreamDecoder decoder;
  char chunk[64 * 1024];

  for (;;) {
    size_t n = std::fread(chunk, 1, sizeof(chunk), f);
    if (n > 0) decoder.Feed(chunk, n);

    bool hit_eof = n < sizeof(chunk);

    for (;;) {
      MutationRecord rec;
      DecodeStatus status = decoder.TryDecode(&rec);
      if (status == DecodeStatus::kOk) {
        ApplyMutationRecord(backend, rec, now_ms);
        ++stats.records_applied;
        continue;
      }
      if (status == DecodeStatus::kMalformed) {
        stats.malformed_record_found = true;
        std::fclose(f);
        return stats;
      }
      break;  // kIncomplete: need more bytes (or this is EOF, see below)
    }

    if (hit_eof) {
      if (decoder.HasBufferedBytes()) stats.truncated_final_record = true;
      break;
    }
  }

  std::fclose(f);
  return stats;
}

}  // namespace persistence

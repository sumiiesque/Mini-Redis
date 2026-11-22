#include "persistence/mutation_record.h"

#include <cstring>

namespace persistence {
namespace {

// Append a 32-bit unsigned integer in little-endian format.
void AppendU32(std::string* out, uint32_t v) {
  char buf[4];
  buf[0] = static_cast<char>(v & 0xFF);
  buf[1] = static_cast<char>((v >> 8) & 0xFF);
  buf[2] = static_cast<char>((v >> 16) & 0xFF);
  buf[3] = static_cast<char>((v >> 24) & 0xFF);
  out->append(buf, 4);
}

// Append a 64-bit integer in little-endian format.
void AppendI64(std::string* out, int64_t v) {
  uint64_t u = static_cast<uint64_t>(v);
  char buf[8];
  for (int i = 0; i < 8; ++i) buf[i] = static_cast<char>((u >> (8 * i)) & 0xFF);
  out->append(buf, 8);
}

// Bounds-checked cursor helper to safely parse binary records.
// This ensures we never read past the end of the buffer even if data is corrupt.
class Cursor {
 public:
  Cursor(const char* data, size_t len) : data_(data), len_(len), pos_(0) {}

  bool ReadU32(uint32_t* out) {
    if (pos_ + 4 > len_) return false;
    *out = static_cast<uint32_t>(static_cast<unsigned char>(data_[pos_])) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data_[pos_ + 1])) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data_[pos_ + 2])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data_[pos_ + 3])) << 24);
    pos_ += 4;
    return true;
  }

  bool ReadI64(int64_t* out) {
    if (pos_ + 8 > len_) return false;
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) {
      u |= static_cast<uint64_t>(static_cast<unsigned char>(data_[pos_ + i])) << (8 * i);
    }
    pos_ += 8;
    *out = static_cast<int64_t>(u);
    return true;
  }

  bool ReadU8(uint8_t* out) {
    if (pos_ + 1 > len_) return false;
    *out = static_cast<uint8_t>(data_[pos_]);
    pos_ += 1;
    return true;
  }

  bool ReadBytes(uint32_t n, std::string* out) {
    if (n > kMaxRecordBodyBytes || pos_ + n > len_) return false;
    out->assign(data_ + pos_, n);
    pos_ += n;
    return true;
  }

  bool AtEnd() const { return pos_ == len_; }

 private:
  const char* data_;
  size_t len_;
  size_t pos_;
};

}  // namespace

// Encodes a mutation into binary format:
// [4 bytes: body length] [1 byte: type] [key length + key] [payload]
std::string SerializeMutationRecord(const MutationRecord& r) {
  std::string body;
  body.push_back(static_cast<char>(r.type));

  AppendU32(&body, static_cast<uint32_t>(r.key.size()));
  body.append(r.key);

  switch (r.type) {
    case MutationType::kSet: {
      body.push_back(r.has_expire ? 1 : 0);
      if (r.has_expire) AppendI64(&body, r.expire_at_ms);
      AppendU32(&body, static_cast<uint32_t>(r.value.size()));
      body.append(r.value);
      break;
    }
    case MutationType::kDel: {
      break;  // Key is sufficient for DEL
    }
    case MutationType::kExpire: {
      AppendI64(&body, r.expire_at_ms);
      break;
    }
  }

  // Prepend total length header so reader knows how many bytes to expect
  std::string framed;
  AppendU32(&framed, static_cast<uint32_t>(body.size()));
  framed += body;
  return framed;
}

void MutationStreamDecoder::Feed(const char* data, size_t len) { buf_.append(data, len); }

// Incremental decoder: reads complete records out of the buffer as bytes arrive.
DecodeStatus MutationStreamDecoder::TryDecode(MutationRecord* out) {
  // Need at least 4 bytes to read the record length
  if (buf_.size() < 4) return DecodeStatus::kIncomplete;

  uint32_t total_len = static_cast<uint32_t>(static_cast<unsigned char>(buf_[0])) |
                        (static_cast<uint32_t>(static_cast<unsigned char>(buf_[1])) << 8) |
                        (static_cast<uint32_t>(static_cast<unsigned char>(buf_[2])) << 16) |
                        (static_cast<uint32_t>(static_cast<unsigned char>(buf_[3])) << 24);

  // Sanity check: prevent huge allocation from corrupted length header
  if (total_len > kMaxRecordBodyBytes) return DecodeStatus::kMalformed;
  if (buf_.size() < 4u + total_len) return DecodeStatus::kIncomplete;

  Cursor cur(buf_.data() + 4, total_len);
  uint8_t raw_type = 0;
  if (!cur.ReadU8(&raw_type)) return DecodeStatus::kMalformed;
  if (raw_type != static_cast<uint8_t>(MutationType::kSet) &&
      raw_type != static_cast<uint8_t>(MutationType::kDel) &&
      raw_type != static_cast<uint8_t>(MutationType::kExpire)) {
    return DecodeStatus::kMalformed;
  }
  MutationType type = static_cast<MutationType>(raw_type);

  MutationRecord rec;
  rec.type = type;

  uint32_t key_len = 0;
  if (!cur.ReadU32(&key_len) || !cur.ReadBytes(key_len, &rec.key)) return DecodeStatus::kMalformed;

  switch (type) {
    case MutationType::kSet: {
      uint8_t has_expire = 0;
      if (!cur.ReadU8(&has_expire)) return DecodeStatus::kMalformed;
      rec.has_expire = has_expire != 0;
      if (rec.has_expire && !cur.ReadI64(&rec.expire_at_ms)) return DecodeStatus::kMalformed;
      uint32_t value_len = 0;
      if (!cur.ReadU32(&value_len) || !cur.ReadBytes(value_len, &rec.value)) {
        return DecodeStatus::kMalformed;
      }
      break;
    }
    case MutationType::kDel: {
      break;
    }
    case MutationType::kExpire: {
      rec.has_expire = true;
      if (!cur.ReadI64(&rec.expire_at_ms)) return DecodeStatus::kMalformed;
      break;
    }
  }

  if (!cur.AtEnd()) return DecodeStatus::kMalformed;  // Extra trailing garbage detected

  // Consume the decoded bytes from the stream buffer
  buf_.erase(0, 4u + total_len);
  *out = std::move(rec);
  return DecodeStatus::kOk;
}

namespace {
// Helper: converts absolute timestamp into relative milliseconds as of right now.
// If the deadline already passed, returns 0 for immediate expiration.
int64_t AbsoluteToRelative(int64_t expire_at_ms, int64_t now_ms) {
  int64_t delta = expire_at_ms - now_ms;
  return delta > 0 ? delta : 0;
}
}  // namespace

// Replays a decoded mutation record directly into the database backend.
void ApplyMutationRecord(KVBackend& backend, const MutationRecord& r, int64_t now_ms) {
  switch (r.type) {
    case MutationType::kSet: {
      int64_t expire_ms = r.has_expire ? AbsoluteToRelative(r.expire_at_ms, now_ms) : -1;
      backend.Set(r.key, r.value, expire_ms);
      break;
    }
    case MutationType::kDel: {
      backend.Del(r.key);
      break;
    }
    case MutationType::kExpire: {
      backend.Expire(r.key, AbsoluteToRelative(r.expire_at_ms, now_ms));
      break;
    }
  }
}

}  // namespace persistence

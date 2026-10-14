#pragma once

#include <cstdint>
#include <string>

enum class KVStatus : uint8_t {
  kGetFound,
  kGetNotFound,
  kSetOk,
  kDelOk,
  kDelNotFound,
  kExpireOk,
  kExpireNotFound,
  kError,
};

struct KVResult {
  KVStatus status = KVStatus::kError;
  std::string value;

  static KVResult GetFound(std::string v) {
    KVResult r;
    r.status = KVStatus::kGetFound;
    r.value = std::move(v);
    return r;
  }
  static KVResult Status(KVStatus s) { return KVResult{s, std::string()}; }
};

class KVBackend {
 public:
  virtual ~KVBackend() = default;

  virtual KVResult Set(const std::string& key, const std::string& value,
                        int64_t expire_ms = -1) = 0;

  virtual KVResult Get(const std::string& key) = 0;

  virtual KVResult Del(const std::string& key) = 0;

  virtual KVResult Expire(const std::string& key, int64_t ttl_ms) = 0;
};

#include "persistence/aof_writer.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

#include "persistence/mutation_record.h"

namespace persistence {

AofWriter::AofWriter(const std::string& path, DurabilityMode mode) : mode_(mode) {
  fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd_ < 0) {
    throw std::runtime_error("AofWriter: failed to open '" + path + "': " + std::strerror(errno));
  }
}

AofWriter::~AofWriter() {
  if (fd_ >= 0) {
    ::fsync(fd_);
    ::close(fd_);
  }
}

void AofWriter::WriteFramed(const std::string& framed) {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t written = 0;
  while (written < framed.size()) {
    ssize_t n = ::write(fd_, framed.data() + written, framed.size() - written);
    if (n > 0) {
      written += static_cast<size_t>(n);
      continue;
    }
    if (n < 0 && errno == EINTR) continue;
    throw std::runtime_error("AofWriter: write() failed: " + std::string(std::strerror(errno)));
  }
  if (mode_ == DurabilityMode::kSyncEveryWrite) {
    if (::fsync(fd_) != 0) {
      throw std::runtime_error("AofWriter: fsync() failed: " + std::string(std::strerror(errno)));
    }
  }
}

void AofWriter::AppendSet(const std::string& key, const std::string& value,
                           int64_t expire_at_ms_or_none) {
  MutationRecord r;
  r.type = MutationType::kSet;
  r.key = key;
  r.value = value;
  r.has_expire = expire_at_ms_or_none >= 0;
  r.expire_at_ms = expire_at_ms_or_none;
  WriteFramed(SerializeMutationRecord(r));
}

void AofWriter::AppendDel(const std::string& key) {
  MutationRecord r;
  r.type = MutationType::kDel;
  r.key = key;
  WriteFramed(SerializeMutationRecord(r));
}

void AofWriter::AppendExpire(const std::string& key, int64_t expire_at_ms) {
  MutationRecord r;
  r.type = MutationType::kExpire;
  r.key = key;
  r.has_expire = true;
  r.expire_at_ms = expire_at_ms;
  WriteFramed(SerializeMutationRecord(r));
}

void AofWriter::Sync() {
  std::lock_guard<std::mutex> lock(mutex_);
  ::fsync(fd_);
}

}  // namespace persistence

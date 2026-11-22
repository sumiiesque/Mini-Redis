#include "replication/replication_replica.h"

#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "persistence/mutation_record.h"

namespace replication {
namespace {

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

constexpr int kReconnectBackoffMs = 200;
constexpr int kSocketTimeoutMs = 200;  // bounds how quickly Stop() is noticed while blocked in recv()

}  // namespace

ReplicationReplica::ReplicationReplica(KVBackend& backend, std::string primary_host,
                                        int primary_port)
    : backend_(backend), host_(std::move(primary_host)), port_(primary_port) {}

ReplicationReplica::~ReplicationReplica() { Stop(); }

void ReplicationReplica::Start() { thread_ = std::thread(&ReplicationReplica::RunLoop, this); }

void ReplicationReplica::Stop() {
  if (stop_requested_.exchange(true)) return;
  if (fd_ >= 0) ::shutdown(fd_, SHUT_RDWR);
  if (thread_.joinable()) thread_.join();
}

void ReplicationReplica::RunLoop() {
  while (!stop_requested_.load(std::memory_order_relaxed)) {
    ConnectAndApplyUntilDisconnected();
    connected_.store(false, std::memory_order_relaxed);
    if (stop_requested_.load(std::memory_order_relaxed)) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(kReconnectBackoffMs));
  }
}

void ReplicationReplica::ConnectAndApplyUntilDisconnected() {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return;

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port_));
  if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
    ::close(fd);
    return;
  }

  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return;  // primary unreachable right now; RunLoop will back off and retry
  }

  timeval tv{};
  tv.tv_sec = kSocketTimeoutMs / 1000;
  tv.tv_usec = (kSocketTimeoutMs % 1000) * 1000;
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  fd_ = fd;
  connected_.store(true, std::memory_order_relaxed);

  persistence::MutationStreamDecoder decoder;
  char buf[64 * 1024];

  while (!stop_requested_.load(std::memory_order_relaxed)) {
    ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n > 0) {
      decoder.Feed(buf, static_cast<size_t>(n));
      for (;;) {
        persistence::MutationRecord rec;
        persistence::DecodeStatus status = decoder.TryDecode(&rec);
        if (status == persistence::DecodeStatus::kOk) {
          persistence::ApplyMutationRecord(backend_, rec, NowMs());
          applied_count_.fetch_add(1, std::memory_order_relaxed);
          continue;
        }
        if (status == persistence::DecodeStatus::kMalformed) {
          // Corrupt stream: treat like a lost connection and let the outer
          // loop reconnect (a fresh connection gets a fresh, correct
          // bootstrap from the primary).
          ::close(fd);
          fd_ = -1;
          return;
        }
        break;  // kIncomplete: wait for more bytes
      }
      continue;
    }
    if (n == 0) break;  // primary closed the connection cleanly
    if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) continue;  // recv timeout; recheck stop flag
    break;  // real error
  }

  ::close(fd);
  fd_ = -1;
}

}  // namespace replication

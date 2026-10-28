#include "replication/replication_primary.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace replication {

ReplicationPrimary::ReplicationPrimary(std::string bind_address, int port, std::string aof_path)
    : bind_address_(std::move(bind_address)), port_(port), aof_path_(std::move(aof_path)) {}

ReplicationPrimary::~ReplicationPrimary() { Stop(); }

void ReplicationPrimary::Start() {
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) throw std::runtime_error("ReplicationPrimary: socket() failed");

  int opt = 1;
  ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port_));
  if (bind_address_.empty() || bind_address_ == "0.0.0.0") {
    addr.sin_addr.s_addr = INADDR_ANY;
  } else {
    ::inet_pton(AF_INET, bind_address_.c_str(), &addr.sin_addr);
  }

  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    throw std::runtime_error("ReplicationPrimary: bind() failed: " + std::string(strerror(errno)));
  }
  if (::listen(listen_fd_, 16) < 0) {
    throw std::runtime_error("ReplicationPrimary: listen() failed");
  }

  socklen_t alen = sizeof(addr);
  if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &alen) == 0) {
    actual_port_.store(ntohs(addr.sin_port), std::memory_order_release);
  }

  accept_thread_ = std::thread(&ReplicationPrimary::AcceptLoop, this);
}

void ReplicationPrimary::AcceptLoop() {
  while (!stop_requested_.load(std::memory_order_relaxed)) {
    pollfd pfd{listen_fd_, POLLIN, 0};
    int rv = ::poll(&pfd, 1, 200);
    ReapDeadReplicas();
    if (rv <= 0) continue;  // timeout or EINTR: loop back and recheck stop flag

    int cfd = ::accept(listen_fd_, nullptr, nullptr);
    if (cfd < 0) continue;
    int one = 1;
    ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    BootstrapAndRegister(cfd);
  }
}

void ReplicationPrimary::BootstrapAndRegister(int client_fd) {
  auto replica = std::make_shared<Replica>();
  replica->fd = client_fd;

  bool bootstrap_ok = true;
  {
    // Holding replicas_mutex_ across the whole bootstrap read+registration
    // is what guarantees each mutation is delivered exactly once: any
    // RecordAndBroadcast() call that starts before we acquire the lock has
    // already finished appending to the AOF (so its bytes are in the file
    // we're about to stream) or is blocked until we release it (so it will
    // reach this replica only via the live queue, registered below).
    std::lock_guard<std::mutex> lock(replicas_mutex_);

    std::FILE* f = std::fopen(aof_path_.c_str(), "rb");
    if (f != nullptr) {
      char buf[64 * 1024];
      size_t n;
      while (bootstrap_ok && (n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        size_t written = 0;
        while (written < n) {
          // MSG_NOSIGNAL: a replica that disconnects mid-bootstrap must
          // yield EPIPE here, not a SIGPIPE that would kill the primary.
          ssize_t w = ::send(client_fd, buf + written, n - written, MSG_NOSIGNAL);
          if (w > 0) {
            written += static_cast<size_t>(w);
            continue;
          }
          if (w < 0 && errno == EINTR) continue;
          bootstrap_ok = false;
          break;
        }
      }
      std::fclose(f);
    }

    if (bootstrap_ok) replicas_.push_back(replica);
  }

  if (!bootstrap_ok) {
    ::close(client_fd);
    return;
  }

  replica->sender_thread = std::thread(&ReplicationPrimary::SenderLoop, this, replica);
}

void ReplicationPrimary::SenderLoop(const std::shared_ptr<Replica>& replica) {
  for (;;) {
    std::string item;
    {
      std::unique_lock<std::mutex> lock(replica->queue_mutex);
      replica->queue_cv.wait(lock, [&] { return replica->stop || !replica->queue.empty(); });
      if (replica->queue.empty()) {
        if (replica->stop) break;
        continue;
      }
      item = std::move(replica->queue.front());
      replica->queue.pop_front();
    }

    size_t written = 0;
    bool failed = false;
    while (written < item.size()) {
      // MSG_NOSIGNAL: see BootstrapAndRegister. A replica disconnecting
      // while we stream to it must not take the primary down with it.
      ssize_t w = ::send(replica->fd, item.data() + written, item.size() - written, MSG_NOSIGNAL);
      if (w > 0) {
        written += static_cast<size_t>(w);
        continue;
      }
      if (w < 0 && errno == EINTR) continue;
      failed = true;
      break;
    }
    if (failed) break;
  }

  ::close(replica->fd);
  replica->alive.store(false, std::memory_order_release);
}

void ReplicationPrimary::ReapDeadReplicas() {
  std::vector<std::shared_ptr<Replica>> dead;
  {
    std::lock_guard<std::mutex> lock(replicas_mutex_);
    auto it = replicas_.begin();
    while (it != replicas_.end()) {
      if (!(*it)->alive.load(std::memory_order_acquire)) {
        dead.push_back(*it);
        it = replicas_.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (auto& r : dead) {
    if (r->sender_thread.joinable()) r->sender_thread.join();
  }
}

void ReplicationPrimary::RecordAndBroadcast(persistence::AofWriter& aof,
                                             const persistence::MutationRecord& record) {
  std::string framed = persistence::SerializeMutationRecord(record);

  std::lock_guard<std::mutex> lock(replicas_mutex_);
  switch (record.type) {
    case persistence::MutationType::kSet:
      aof.AppendSet(record.key, record.value, record.has_expire ? record.expire_at_ms : -1);
      break;
    case persistence::MutationType::kDel:
      aof.AppendDel(record.key);
      break;
    case persistence::MutationType::kExpire:
      aof.AppendExpire(record.key, record.expire_at_ms);
      break;
  }

  for (auto& r : replicas_) {
    std::lock_guard<std::mutex> qlock(r->queue_mutex);
    r->queue.push_back(framed);
    r->queue_cv.notify_one();
  }
}

size_t ReplicationPrimary::ReplicaCount() const {
  std::lock_guard<std::mutex> lock(replicas_mutex_);
  return replicas_.size();
}

void ReplicationPrimary::Stop() {
  if (stop_requested_.exchange(true)) return;  // already stopped

  if (listen_fd_ >= 0) {
    ::shutdown(listen_fd_, SHUT_RDWR);
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
  if (accept_thread_.joinable()) accept_thread_.join();

  std::vector<std::shared_ptr<Replica>> all;
  {
    std::lock_guard<std::mutex> lock(replicas_mutex_);
    all = replicas_;
    replicas_.clear();
  }
  for (auto& r : all) {
    {
      std::lock_guard<std::mutex> qlock(r->queue_mutex);
      r->stop = true;
    }
    r->queue_cv.notify_all();
    ::shutdown(r->fd, SHUT_RDWR);
    if (r->sender_thread.joinable()) r->sender_thread.join();
  }
}

}  // namespace replication

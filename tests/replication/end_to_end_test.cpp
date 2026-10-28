// Full end-to-end integration test: a real net::Server (RESP over TCP,
// backed by a real LockingBackend) with persistence+replication wired in
// via Server's MutationSink hook, a real ReplicationPrimary streaming to a
// real ReplicationReplica applying into its own LockingBackend. Exercises
// the exact path a real deployment would use: RESP client -> Server ->
// ExecuteCommand -> MutationSink -> AOF + live replica broadcast.

#include "network/server.h"
#include "persistence/aof_writer.h"
#include "replication/replication_primary.h"
#include "replication/replication_replica.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <functional>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "backends/locking_backend.h"

namespace {
int g_failures = 0;
int g_checks = 0;
}  // namespace

#define CHECK(cond)                                                                  \
  do {                                                                               \
    ++g_checks;                                                                      \
    if (!(cond)) {                                                                   \
      ++g_failures;                                                                  \
      std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
    }                                                                                \
  } while (0)

using backends::LockingBackend;
using persistence::AofWriter;
using persistence::DurabilityMode;
using persistence::MutationRecord;
using persistence::MutationType;
using replication::ReplicationPrimary;
using replication::ReplicationReplica;

namespace {

void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

bool WaitUntil(int timeout_ms, const std::function<bool()>& pred) {
  int waited = 0;
  while (waited < timeout_ms) {
    if (pred()) return true;
    SleepMs(10);
    waited += 10;
  }
  return pred();
}

std::string TempPath(const std::string& name) {
  return "/tmp/mini_redis_e2e_" + name + "_" + std::to_string(::getpid()) + ".aof";
}

std::string Encode(const std::vector<std::string>& parts) {
  std::string out = "*" + std::to_string(parts.size()) + "\r\n";
  for (const auto& p : parts) out += "$" + std::to_string(p.size()) + "\r\n" + p + "\r\n";
  return out;
}

int ConnectTo(int port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  for (int attempt = 0; attempt < 50; ++attempt) {
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) return fd;
    SleepMs(20);
  }
  return -1;
}

void SendAll(int fd, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
    ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
    if (n <= 0) return;
    sent += static_cast<size_t>(n);
  }
}

std::string RecvN(int fd, size_t n) {
  std::string out;
  char buf[4096];
  while (out.size() < n) {
    ssize_t r = ::recv(fd, buf, sizeof(buf), 0);
    if (r <= 0) break;
    out.append(buf, static_cast<size_t>(r));
  }
  return out;
}

}  // namespace

int main() {
  std::string aof_path = TempPath("main");
  ::unlink(aof_path.c_str());

  LockingBackend primary_backend;
  AofWriter aof(aof_path, DurabilityMode::kBuffered);
  ReplicationPrimary primary("127.0.0.1", 0, aof_path);
  primary.Start();

  auto mutation_sink = [&](const net::MutationInfo& m) {
    MutationRecord r;
    switch (m.kind) {
      case net::MutationInfo::Kind::kSet:
        r.type = MutationType::kSet;
        r.key = m.key;
        r.value = m.value;
        r.has_expire = m.has_expire;
        r.expire_at_ms = m.expire_at_ms;
        break;
      case net::MutationInfo::Kind::kDel:
        r.type = MutationType::kDel;
        r.key = m.key;
        break;
      case net::MutationInfo::Kind::kExpire:
        r.type = MutationType::kExpire;
        r.key = m.key;
        r.expire_at_ms = m.expire_at_ms;
        break;
      case net::MutationInfo::Kind::kNone:
        return;
    }
    primary.RecordAndBroadcast(aof, r);
  };

  net::ServerConfig server_config;
  server_config.port = 0;
  server_config.num_workers = 2;
  net::Server server(primary_backend, server_config, mutation_sink);
  std::thread server_thread([&] { server.Run(); });
  CHECK(WaitUntil(2000, [&] { return server.ListenPort() != 0; }));

  int client_fd = ConnectTo(server.ListenPort());
  CHECK(client_fd >= 0);

  // Mutations issued before any replica connects — must reach the replica
  // via AOF-based bootstrap.
  SendAll(client_fd, Encode({"SET", "alpha", "1"}));
  CHECK(RecvN(client_fd, 5) == "+OK\r\n");
  SendAll(client_fd, Encode({"SET", "beta", "2", "PX", "60000"}));
  CHECK(RecvN(client_fd, 5) == "+OK\r\n");

  LockingBackend replica_backend;
  ReplicationReplica replica(replica_backend, "127.0.0.1", primary.ListenPort());
  replica.Start();

  CHECK(WaitUntil(2000, [&] { return replica_backend.Get("alpha").status == KVStatus::kGetFound; }));
  CHECK(replica_backend.Get("beta").status == KVStatus::kGetFound);

  // Live mutations after the replica is connected, including EXPIRE.
  CHECK(WaitUntil(2000, [&] { return primary.ReplicaCount() >= 1; }));
  SendAll(client_fd, Encode({"SET", "gamma", "3"}));
  CHECK(RecvN(client_fd, 5) == "+OK\r\n");
  SendAll(client_fd, Encode({"EXPIRE", "gamma", "0"}));  // expires immediately
  CHECK(RecvN(client_fd, 4) == ":1\r\n");
  SendAll(client_fd, Encode({"DEL", "alpha"}));
  CHECK(RecvN(client_fd, 4) == ":1\r\n");

  // Primary-side view via RESP.
  SendAll(client_fd, Encode({"GET", "gamma"}));
  CHECK(RecvN(client_fd, 5) == "$-1\r\n");  // expired
  SendAll(client_fd, Encode({"GET", "alpha"}));
  CHECK(RecvN(client_fd, 5) == "$-1\r\n");  // deleted

  // Replica must converge to the same observable state.
  CHECK(WaitUntil(2000, [&] { return replica_backend.Get("alpha").status == KVStatus::kGetNotFound; }));
  CHECK(WaitUntil(2000, [&] { return replica_backend.Get("gamma").status == KVStatus::kGetNotFound; }));
  auto beta_primary = primary_backend.Get("beta");
  auto beta_replica = replica_backend.Get("beta");
  CHECK(beta_primary.status == beta_replica.status);
  CHECK(beta_primary.status == KVStatus::kGetFound && beta_primary.value == beta_replica.value);

  ::close(client_fd);
  replica.Stop();
  server.Stop();
  server_thread.join();
  primary.Stop();
  ::unlink(aof_path.c_str());

  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}

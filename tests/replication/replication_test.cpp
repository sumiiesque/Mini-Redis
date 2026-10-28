// Replication tests: primary + replica over real TCP, backed by real
// LockingBackend instances, exercising bootstrap, live streaming, TTL
// correctness, fragmented delivery (implicitly, since TCP itself may
// fragment/coalesce — the framing is what's actually under test), and
// reconnect behavior.

#include "replication/replication_primary.h"
#include "replication/replication_replica.h"

#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <unistd.h>

#include "backends/locking_backend.h"
#include "persistence/aof_writer.h"

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

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string TempPath(const std::string& name) {
  return "/tmp/mini_redis_repl_test_" + name + "_" + std::to_string(::getpid()) + ".aof";
}

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

MutationRecord MakeSet(const std::string& k, const std::string& v, int64_t expire_at_ms = -1) {
  MutationRecord r;
  r.type = MutationType::kSet;
  r.key = k;
  r.value = v;
  r.has_expire = expire_at_ms >= 0;
  r.expire_at_ms = expire_at_ms;
  return r;
}

MutationRecord MakeDel(const std::string& k) {
  MutationRecord r;
  r.type = MutationType::kDel;
  r.key = k;
  return r;
}

}  // namespace

void TestBootstrapThenLiveReplication() {
  std::string aof_path = TempPath("bootstrap");
  ::unlink(aof_path.c_str());

  LockingBackend primary_backend;
  AofWriter aof(aof_path, DurabilityMode::kBuffered);
  ReplicationPrimary primary("127.0.0.1", 0, aof_path);
  primary.Start();

  // Mutations before any replica connects — these must be delivered via
  // bootstrap (AOF replay), not live streaming.
  primary_backend.Set("pre1", "v1");
  primary.RecordAndBroadcast(aof, MakeSet("pre1", "v1"));
  primary_backend.Set("pre2", "v2");
  primary.RecordAndBroadcast(aof, MakeSet("pre2", "v2"));

  LockingBackend replica_backend;
  ReplicationReplica replica(replica_backend, "127.0.0.1", primary.ListenPort());
  replica.Start();

  CHECK(WaitUntil(2000, [&] { return replica.AppliedCount() >= 2; }));
  CHECK(replica_backend.Get("pre1").status == KVStatus::kGetFound);
  CHECK(replica_backend.Get("pre2").status == KVStatus::kGetFound);

  // Live mutation after the replica is connected.
  CHECK(WaitUntil(2000, [&] { return primary.ReplicaCount() >= 1; }));
  primary_backend.Set("live", "v3");
  primary.RecordAndBroadcast(aof, MakeSet("live", "v3"));

  CHECK(WaitUntil(2000, [&] { return replica_backend.Get("live").status == KVStatus::kGetFound; }));

  replica.Stop();
  primary.Stop();
  ::unlink(aof_path.c_str());
}

void TestDelReplicates() {
  std::string aof_path = TempPath("del");
  ::unlink(aof_path.c_str());

  LockingBackend primary_backend;
  AofWriter aof(aof_path, DurabilityMode::kBuffered);
  ReplicationPrimary primary("127.0.0.1", 0, aof_path);
  primary.Start();

  primary_backend.Set("k", "v");
  primary.RecordAndBroadcast(aof, MakeSet("k", "v"));

  LockingBackend replica_backend;
  ReplicationReplica replica(replica_backend, "127.0.0.1", primary.ListenPort());
  replica.Start();
  CHECK(WaitUntil(2000, [&] { return replica_backend.Get("k").status == KVStatus::kGetFound; }));

  primary_backend.Del("k");
  primary.RecordAndBroadcast(aof, MakeDel("k"));
  CHECK(WaitUntil(2000, [&] { return replica_backend.Get("k").status == KVStatus::kGetNotFound; }));

  replica.Stop();
  primary.Stop();
  ::unlink(aof_path.c_str());
}

void TestTtlAndExpireReplicateWithAbsoluteTimestamps() {
  std::string aof_path = TempPath("ttl");
  ::unlink(aof_path.c_str());

  LockingBackend primary_backend;
  AofWriter aof(aof_path, DurabilityMode::kBuffered);
  ReplicationPrimary primary("127.0.0.1", 0, aof_path);
  primary.Start();

  int64_t expire_at = NowMs() + 60000;
  primary_backend.Set("k", "v", 60000);
  primary.RecordAndBroadcast(aof, MakeSet("k", "v", expire_at));

  LockingBackend replica_backend;
  ReplicationReplica replica(replica_backend, "127.0.0.1", primary.ListenPort());
  replica.Start();
  CHECK(WaitUntil(2000, [&] { return replica_backend.Get("k").status == KVStatus::kGetFound; }));

  // Also replicate an already-past absolute expiry and confirm the replica
  // ends up treating that key as expired/missing, not silently keeping it
  // alive.
  MutationRecord expired = MakeSet("stale", "gone", NowMs() - 100000);
  primary_backend.Set("stale", "gone", 0);
  primary.RecordAndBroadcast(aof, expired);
  CHECK(WaitUntil(2000, [&] { return replica_backend.Get("stale").status == KVStatus::kGetNotFound; }));

  replica.Stop();
  primary.Stop();
  ::unlink(aof_path.c_str());
}

void TestReplicaReconnectAfterPrimaryRestart() {
  std::string aof_path = TempPath("reconnect");
  ::unlink(aof_path.c_str());

  int fixed_port = 0;
  LockingBackend replica_backend;
  // We need a fixed, known port so a later replica can reconnect to a
  // recreated primary; take one from a first primary instance.
  {
    LockingBackend primary_backend;
    AofWriter aof(aof_path, DurabilityMode::kBuffered);
    ReplicationPrimary primary("127.0.0.1", 0, aof_path);
    primary.Start();
    fixed_port = primary.ListenPort();

    primary_backend.Set("a", "1");
    primary.RecordAndBroadcast(aof, MakeSet("a", "1"));

    ReplicationReplica r2(replica_backend, "127.0.0.1", fixed_port);
    r2.Start();
    CHECK(WaitUntil(2000, [&] { return replica_backend.Get("a").status == KVStatus::kGetFound; }));
    r2.Stop();
    primary.Stop();
  }
  // Primary is gone; a NEW primary later reuses the same port and AOF file.
  // A replica started against the (currently unreachable) port must not
  // crash while retrying, and must catch up once the new primary appears.
  ReplicationReplica replica2(replica_backend, "127.0.0.1", fixed_port);
  replica2.Start();
  SleepMs(150);  // give it a couple of failed connect attempts first

  LockingBackend primary_backend2;
  AofWriter aof2(aof_path, DurabilityMode::kBuffered);
  ReplicationPrimary primary2("127.0.0.1", fixed_port, aof_path);
  primary2.Start();
  primary_backend2.Set("b", "2");
  primary2.RecordAndBroadcast(aof2, MakeSet("b", "2"));

  CHECK(WaitUntil(3000, [&] { return replica_backend.Get("b").status == KVStatus::kGetFound; }));

  replica2.Stop();
  primary2.Stop();
  ::unlink(aof_path.c_str());
}

int main() {
  TestBootstrapThenLiveReplication();
  TestDelReplicates();
  TestTtlAndExpireReplicateWithAbsoluteTimestamps();
  TestReplicaReconnectAfterPrimaryRestart();

  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}

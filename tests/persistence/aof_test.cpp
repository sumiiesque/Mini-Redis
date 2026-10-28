// AOF write + recovery tests. Uses the real LockingBackend (not a trivial
// test double) both to exercise persistence against a genuine backend and
// to demonstrate that persistence code only ever goes through KVBackend.

#include "persistence/aof_recovery.h"
#include "persistence/aof_writer.h"

#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <unistd.h>

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
using persistence::RecoverFromFile;

namespace {

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string TempPath(const std::string& name) {
  return "/tmp/mini_redis_aof_test_" + name + "_" + std::to_string(::getpid()) + ".aof";
}

void RemoveIfExists(const std::string& path) { ::unlink(path.c_str()); }

}  // namespace

void TestBasicWriteAndRecover() {
  std::string path = TempPath("basic");
  RemoveIfExists(path);
  {
    AofWriter w(path, DurabilityMode::kBuffered);
    w.AppendSet("a", "1", -1);
    w.AppendSet("b", "2", -1);
    w.AppendDel("a");
  }

  LockingBackend db;
  auto stats = RecoverFromFile(path, db);
  CHECK(stats.file_existed);
  CHECK(stats.records_applied == 3);
  CHECK(!stats.truncated_final_record && !stats.malformed_record_found);

  CHECK(db.Get("a").status == KVStatus::kGetNotFound);
  auto r = db.Get("b");
  CHECK(r.status == KVStatus::kGetFound && r.value == "2");

  RemoveIfExists(path);
}

void TestMissingFileIsNotAnError() {
  std::string path = TempPath("missing");
  RemoveIfExists(path);
  LockingBackend db;
  auto stats = RecoverFromFile(path, db);
  CHECK(!stats.file_existed);
  CHECK(stats.records_applied == 0);
}

void TestTtlSurvivesReplayWhenNotYetExpired() {
  std::string path = TempPath("ttl_future");
  RemoveIfExists(path);
  {
    AofWriter w(path, DurabilityMode::kBuffered);
    w.AppendSet("k", "v", NowMs() + 60000);  // absolute expiry far in the future
  }

  LockingBackend db;
  auto stats = RecoverFromFile(path, db);
  CHECK(stats.records_applied == 1);
  auto r = db.Get("k");
  CHECK(r.status == KVStatus::kGetFound && r.value == "v");

  RemoveIfExists(path);
}

void TestPastAbsoluteExpiryReplaysAsExpired() {
  std::string path = TempPath("ttl_past");
  RemoveIfExists(path);
  {
    AofWriter w(path, DurabilityMode::kBuffered);
    // Logged absolute expiry is already in the past by the time we recover.
    w.AppendSet("k", "v", NowMs() - 100000);
  }

  LockingBackend db;
  auto stats = RecoverFromFile(path, db);
  CHECK(stats.records_applied == 1);
  CHECK(db.Get("k").status == KVStatus::kGetNotFound);

  RemoveIfExists(path);
}

void TestExpireRecordReplay() {
  std::string path = TempPath("expire_record");
  RemoveIfExists(path);
  {
    AofWriter w(path, DurabilityMode::kBuffered);
    w.AppendSet("k", "v", -1);
    w.AppendExpire("k", NowMs() + 60000);
  }

  LockingBackend db;
  auto stats = RecoverFromFile(path, db);
  CHECK(stats.records_applied == 2);
  CHECK(db.Get("k").status == KVStatus::kGetFound);

  RemoveIfExists(path);
}

void TestEmptyKeyAndValueRoundTrip() {
  std::string path = TempPath("empty_kv");
  RemoveIfExists(path);
  {
    AofWriter w(path, DurabilityMode::kBuffered);
    w.AppendSet("", "", -1);
  }

  LockingBackend db;
  RecoverFromFile(path, db);
  auto r = db.Get("");
  CHECK(r.status == KVStatus::kGetFound && r.value.empty());

  RemoveIfExists(path);
}

void TestTruncatedFinalRecordStopsCleanlyButKeepsPriorRecords() {
  std::string path = TempPath("truncated");
  RemoveIfExists(path);
  {
    AofWriter w(path, DurabilityMode::kBuffered);
    w.AppendSet("good", "1", -1);
    w.AppendSet("also-good", "2", -1);
  }

  // Append a partial, deliberately incomplete record (looks like the start
  // of a SET whose declared length promises more bytes than follow) to
  // simulate a crash mid-write.
  {
    int fd = ::open(path.c_str(), O_WRONLY | O_APPEND, 0644);
    CHECK(fd >= 0);
    const char partial[] = "\x20\x00\x00\x00\x01\x03\x00\x00\x00key";  // declares 32-byte body, only ~7 present
    ::write(fd, partial, sizeof(partial) - 1);
    ::close(fd);
  }

  LockingBackend db;
  auto stats = RecoverFromFile(path, db);
  CHECK(stats.records_applied == 2);
  CHECK(stats.truncated_final_record);
  CHECK(!stats.malformed_record_found);
  CHECK(db.Get("good").status == KVStatus::kGetFound);
  CHECK(db.Get("also-good").status == KVStatus::kGetFound);

  RemoveIfExists(path);
}

void TestMalformedRecordStopsReplayWithoutCrashing() {
  std::string path = TempPath("malformed");
  RemoveIfExists(path);
  {
    AofWriter w(path, DurabilityMode::kBuffered);
    w.AppendSet("good", "1", -1);
  }
  {
    int fd = ::open(path.c_str(), O_WRONLY | O_APPEND, 0644);
    CHECK(fd >= 0);
    // A record declaring a 10-byte body whose type byte is invalid (0xEE).
    const char bad[] = "\x0a\x00\x00\x00\xee\x00\x00\x00\x00\x00\x00\x00\x00\x00";
    ::write(fd, bad, sizeof(bad) - 1);
    ::close(fd);
  }

  LockingBackend db;
  auto stats = RecoverFromFile(path, db);
  CHECK(stats.records_applied == 1);
  CHECK(stats.malformed_record_found);
  CHECK(db.Get("good").status == KVStatus::kGetFound);

  RemoveIfExists(path);
}

void TestSyncEveryWriteModeWorks() {
  std::string path = TempPath("sync_mode");
  RemoveIfExists(path);
  {
    AofWriter w(path, DurabilityMode::kSyncEveryWrite);
    w.AppendSet("k", "v", -1);
    w.AppendDel("k");
  }
  LockingBackend db;
  auto stats = RecoverFromFile(path, db);
  CHECK(stats.records_applied == 2);
  CHECK(db.Get("k").status == KVStatus::kGetNotFound);

  RemoveIfExists(path);
}

int main() {
  TestBasicWriteAndRecover();
  TestMissingFileIsNotAnError();
  TestTtlSurvivesReplayWhenNotYetExpired();
  TestPastAbsoluteExpiryReplaysAsExpired();
  TestExpireRecordReplay();
  TestEmptyKeyAndValueRoundTrip();
  TestTruncatedFinalRecordStopsCleanlyButKeepsPriorRecords();
  TestMalformedRecordStopsReplayWithoutCrashing();
  TestSyncEveryWriteModeWorks();

  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}

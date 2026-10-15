#include "backends/locking_backend.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace {
int g_failures = 0;
int g_checks = 0;
}

#define CHECK(cond)                                                                    do {                                                                                   ++g_checks;                                                                          if (!(cond)) {                                                                         ++g_failures;                                                                        std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__);     }                                                                                  } while (0)

using backends::LockingBackend;

void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void TestSetGetOverwrite() {
  LockingBackend db;
  CHECK(db.Get("missing").status == KVStatus::kGetNotFound);

  CHECK(db.Set("k", "v1").status == KVStatus::kSetOk);
  auto r = db.Get("k");
  CHECK(r.status == KVStatus::kGetFound && r.value == "v1");

  CHECK(db.Set("k", "v2").status == KVStatus::kSetOk);
  r = db.Get("k");
  CHECK(r.status == KVStatus::kGetFound && r.value == "v2");
}

void TestEmptyKeyAndValue() {
  LockingBackend db;
  CHECK(db.Set("", "v").status == KVStatus::kSetOk);
  auto r = db.Get("");
  CHECK(r.status == KVStatus::kGetFound && r.value == "v");

  CHECK(db.Set("k", "").status == KVStatus::kSetOk);
  r = db.Get("k");
  CHECK(r.status == KVStatus::kGetFound && r.value.empty());
}

void TestDel() {
  LockingBackend db;
  CHECK(db.Del("missing").status == KVStatus::kDelNotFound);

  db.Set("k", "v");
  CHECK(db.Del("k").status == KVStatus::kDelOk);
  CHECK(db.Get("k").status == KVStatus::kGetNotFound);
  CHECK(db.Del("k").status == KVStatus::kDelNotFound);
}

void TestRepeatedOperations() {
  LockingBackend db;
  for (int i = 0; i < 10; ++i) CHECK(db.Set("k", "v").status == KVStatus::kSetOk);
  CHECK(db.Get("k").value == "v");
  CHECK(db.Del("k").status == KVStatus::kDelOk);
  for (int i = 0; i < 3; ++i) CHECK(db.Del("k").status == KVStatus::kDelNotFound);
}

void TestSetWithTtlExpires() {
  LockingBackend db;
  db.Set("k", "v", 60);
  CHECK(db.Get("k").status == KVStatus::kGetFound);
  SleepMs(150);
  CHECK(db.Get("k").status == KVStatus::kGetNotFound);
}

void TestSetClearsPriorTtl() {
  LockingBackend db;
  db.Set("k", "v", 60);
  db.Set("k", "v2");
  SleepMs(150);
  auto r = db.Get("k");
  CHECK(r.status == KVStatus::kGetFound && r.value == "v2");
}

void TestExpireOnExistingKey() {
  LockingBackend db;
  db.Set("k", "v");
  CHECK(db.Expire("k", 60).status == KVStatus::kExpireOk);
  CHECK(db.Get("k").status == KVStatus::kGetFound);
  SleepMs(150);
  CHECK(db.Get("k").status == KVStatus::kGetNotFound);
}

void TestExpireOnMissingOrExpiredKey() {
  LockingBackend db;
  CHECK(db.Expire("missing", 100).status == KVStatus::kExpireNotFound);

  db.Set("k", "v", 30);
  SleepMs(100);
  CHECK(db.Expire("k", 100).status == KVStatus::kExpireNotFound);
}

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

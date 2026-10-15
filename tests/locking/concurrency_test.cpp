#include "backends/locking_backend.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {
int g_failures = 0;
int g_checks = 0;
}

#define CHECK(cond)                                                                    do {                                                                                   ++g_checks;                                                                          if (!(cond)) {                                                                         ++g_failures;                                                                        std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__);     }                                                                                  } while (0)

using backends::LockingBackend;

std::string MakeValue(char c, size_t len) { return std::string(len, c); }

bool IsUniform(const std::string& v) {
  if (v.empty()) return true;
  for (char c : v) {
    if (c != v[0]) return false;
  }
  return true;
}

void TestConcurrentSetGetDelSameKey() {
  LockingBackend db;
  constexpr int kThreads = 8;
  constexpr int kOpsPerThread = 2000;
  std::atomic<bool> saw_corruption{false};

  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      char c = static_cast<char>('a' + t);
      std::string val = MakeValue(c, 32);
      for (int i = 0; i < kOpsPerThread; ++i) {
        db.Set("shared", val);
        KVResult r = db.Get("shared");
        if (r.status == KVStatus::kGetFound && !IsUniform(r.value)) {
          saw_corruption.store(true);
        }
        if (i % 7 == 0) db.Del("shared");
        if (i % 11 == 0) db.Expire("shared", 10000);
      }
    });
  }
  for (auto& th : threads) th.join();

  CHECK(!saw_corruption.load());
}

void TestConcurrentDifferentStripes() {
  LockingBackend db;
  constexpr int kThreads = 8;
  constexpr int kKeysPerThread = 500;

  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < kKeysPerThread; ++i) {
        std::string key = "t" + std::to_string(t) + "-k" + std::to_string(i);
        db.Set(key, key);
      }
    });
  }
  for (auto& th : threads) th.join();

  bool all_found = true;
  for (int t = 0; t < kThreads; ++t) {
    for (int i = 0; i < kKeysPerThread; ++i) {
      std::string key = "t" + std::to_string(t) + "-k" + std::to_string(i);
      KVResult r = db.Get(key);
      if (r.status != KVStatus::kGetFound || r.value != key) all_found = false;
    }
  }
  CHECK(all_found);
}

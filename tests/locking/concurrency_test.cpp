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

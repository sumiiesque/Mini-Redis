#include "backends/locking_backend.h"
#include "backends/mvcc_backend.h"

#include <chrono>
#include <iostream>
#include <thread>

using backends::LockingBackend;
using backends::MVCCBackend;

int passed = 0;
int failed = 0;

void check(bool condition, const char* message) {
    if (condition) {
        ++passed;
        std::cout << "PASS: " << message << '\n';
    } else {
        ++failed;
        std::cout << "FAIL: " << message << '\n';
    }
}

void testBackend(KVBackend& backend, const char* name) {
    std::cout << "\nTesting " << name << '\n';

    auto result = backend.Set("user", "Smriti");
    check(result.status == KVStatus::kSetOk,
          "SET returns success");

    result = backend.Get("user");
    check(result.status == KVStatus::kGetFound &&
          result.value == "Smriti",
          "GET returns stored value");

    result = backend.Set("user", "Updated");
    check(result.status == KVStatus::kSetOk,
          "SET overwrites existing value");

    result = backend.Get("user");
    check(result.status == KVStatus::kGetFound &&
          result.value == "Updated",
          "GET returns updated value");

    result = backend.Del("user");
    check(result.status == KVStatus::kDelOk,
          "DEL removes existing key");

    result = backend.Get("user");
    check(result.status == KVStatus::kGetNotFound,
          "GET after DEL returns not found");

    result = backend.Expire("missing", 1000);
    check(result.status == KVStatus::kExpireNotFound,
          "EXPIRE on missing key returns not found");

    backend.Set("ttl_key", "temporary");
    result = backend.Expire("ttl_key", 100);

    check(result.status == KVStatus::kExpireOk,
          "EXPIRE succeeds on existing key");

    std::this_thread::sleep_for(
        std::chrono::milliseconds(150));

    result = backend.Get("ttl_key");
    check(result.status == KVStatus::kGetNotFound,
          "Expired key behaves like missing key");
}

int main() {
    LockingBackend locking;
    MVCCBackend mvcc;

    testBackend(locking, "LockingBackend");
    testBackend(mvcc, "MVCCBackend");

    std::cout << "\nTotal: " << passed << " passed, "
              << failed << " failed\n";

    return failed == 0 ? 0 : 1;
}

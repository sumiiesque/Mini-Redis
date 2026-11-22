
#include "backends/mvcc_backend.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using backends::MVCCBackend;

int passed = 0;
int failed = 0;

void check(bool condition, const std::string& description) {
    if (condition) {
        ++passed;
        std::cout << "PASS: " << description << '\n';
    } else {
        ++failed;
        std::cout << "FAIL: " << description << '\n';
    }
}

int main() {
    MVCCBackend db;

    // 1. Set and Get
    auto result = db.Set("name", "Smriti");
    check(result.status == KVStatus::kSetOk, "SET returns success");

    result = db.Get("name");
    check(result.status == KVStatus::kGetFound &&
          result.value == "Smriti", "GET returns the stored value");

    // 2. Overwrite
    db.Set("name", "Updated");
    result = db.Get("name");
    check(result.status == KVStatus::kGetFound &&
          result.value == "Updated", "GET returns the latest value");

    // 3. Missing key
    result = db.Get("missing");
    check(result.status == KVStatus::kGetNotFound,
          "GET on a missing key returns not found");

    // 4. Delete
    result = db.Del("name");
    check(result.status == KVStatus::kDelOk, "DEL existing key succeeds");

    result = db.Get("name");
    check(result.status == KVStatus::kGetNotFound,
          "GET after DEL returns not found");

    result = db.Del("name");
    check(result.status == KVStatus::kDelNotFound,
          "DEL on a deleted key returns not found");

    // 5. Empty key and value
    result = db.Set("", "");
    check(result.status == KVStatus::kSetOk, "SET accepts empty key and value");

    result = db.Get("");
    check(result.status == KVStatus::kGetFound && result.value.empty(),
          "GET returns an empty value");

    // 6. Expire an existing key
    db.Set("ttl", "temporary");
    result = db.Expire("ttl", 50);
    check(result.status == KVStatus::kExpireOk,
          "EXPIRE existing key succeeds");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    result = db.Get("ttl");
    check(result.status == KVStatus::kGetNotFound,
          "Expired key is reported as not found");

    // 7. Expire a missing key
    result = db.Expire("missing", 1000);
    check(result.status == KVStatus::kExpireNotFound,
          "EXPIRE on a missing key returns not found");

    // 8. SET with TTL
    result = db.Set("short", "lifetime", 50);
    check(result.status == KVStatus::kSetOk, "SET with TTL succeeds");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    result = db.Get("short");
    check(result.status == KVStatus::kGetNotFound,
          "SET with TTL expires the key");

    // 9. Repeated overwrites
    for (int i = 0; i < 200; ++i) {
        db.Set("repeated", std::to_string(i));
    }
    result = db.Get("repeated");
    check(result.status == KVStatus::kGetFound &&
          result.value == "199", "Repeated updates return the latest value");

    std::cout << "\nResults: " << passed << " passed, "
              << failed << " failed\n";

    return failed == 0 ? 0 : 1;
}

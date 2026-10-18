
#include "backends/mvcc_backend.h"

#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using backends::MVCCBackend;

int main() {
    MVCCBackend db;

    constexpr int thread_count = 8;
    constexpr int iterations = 5000;

    std::atomic<int> failures{0};
    std::vector<std::thread> workers;

    // Create a shared key that every thread will access.
    db.Set("shared", "initial");

    for (int t = 0; t < thread_count; ++t) {
        workers.emplace_back([&, t]() {
            for (int i = 0; i < iterations; ++i) {
                // Every thread writes to the same key.
                std::string value =
                    std::to_string(t) + ":" + std::to_string(i);

                auto set_result = db.Set("shared", value);
                if (set_result.status != KVStatus::kSetOk) {
                    ++failures;
                }

                // Concurrently read the shared key.
                auto get_result = db.Get("shared");
                if (get_result.status != KVStatus::kGetFound) {
                    ++failures;
                }

                // Each thread also operates on its own key.
                std::string own_key = "thread_" + std::to_string(t);

                set_result = db.Set(own_key, value);
                if (set_result.status != KVStatus::kSetOk) {
                    ++failures;
                }

                get_result = db.Get(own_key);
                if (get_result.status != KVStatus::kGetFound) {
                    ++failures;
                }
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    auto final_result = db.Get("shared");

    bool final_value_valid =
        final_result.status == KVStatus::kGetFound &&
        !final_result.value.empty();

    std::cout << "Threads: " << thread_count << '\n';
    std::cout << "Iterations per thread: " << iterations << '\n';
    std::cout << "Total operations attempted: "
              << thread_count * iterations * 4 << '\n';
    std::cout << "Operation failures: " << failures.load() << '\n';
    std::cout << "Final shared key readable: "
              << (final_value_valid ? "yes" : "no") << '\n';

    if (failures.load() == 0 && final_value_valid) {
        std::cout << "PASS: concurrency stress test\n";
        return 0;
    }

    std::cout << "FAIL: concurrency stress test\n";
    return 1;
}

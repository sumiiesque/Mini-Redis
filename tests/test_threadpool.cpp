#include "thread/thread_pool.h"
#include "thread/cpu_affinity.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <numeric>
#include <vector>

static int g_test_passed = 0;
static int g_test_failed = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (cond) { \
            g_test_passed++; \
            std::cout << "  [PASS] " << msg << std::endl; \
        } else { \
            g_test_failed++; \
            std::cerr << "  [FAIL] " << msg << " (line " << __LINE__ << ")" << std::endl; \
        } \
    } while (0)

void test_pool_creation_and_size() {
    std::cout << "Running test_pool_creation_and_size..." << std::endl;
    ThreadPool pool(4);
    TEST_CHECK(pool.workerCount() == 4, "Worker count should equal 4");
    TEST_CHECK(pool.pendingTaskCount() == 0, "Initial task queue should be empty");
    TEST_CHECK(!pool.isStopping(), "Pool should not be stopping initially");
}

void test_basic_task_execution() {
    std::cout << "Running test_basic_task_execution..." << std::endl;
    ThreadPool pool(2);
    std::atomic<int> counter{0};

    for (int i = 0; i < 50; ++i) {
        pool.enqueue([&counter]() {
            counter++;
        });
    }

    pool.waitIdle();
    TEST_CHECK(counter.load() == 50, "All 50 enqueued tasks executed");
}

void test_submit_with_future() {
    std::cout << "Running test_submit_with_future..." << std::endl;
    ThreadPool pool(4);

    std::vector<std::future<int>> futures;
    for (int i = 0; i < 20; ++i) {
        futures.push_back(pool.submit([i]() -> int {
            return i * i;
        }));
    }

    bool all_correct = true;
    for (int i = 0; i < 20; ++i) {
        int val = futures[i].get();
        if (val != i * i) {
            all_correct = false;
        }
    }
    TEST_CHECK(all_correct, "All futures returned expected calculated values");
}

void test_exception_isolation() {
    std::cout << "Running test_exception_isolation..." << std::endl;
    ThreadPool pool(2);
    std::atomic<bool> follow_up_executed{false};

    pool.enqueue([]() {
        throw std::runtime_error("simulated task error");
    });

    pool.enqueue([&follow_up_executed]() {
        follow_up_executed = true;
    });

    pool.waitIdle();
    TEST_CHECK(follow_up_executed.load(), "Worker remains alive after caught exception");
}

void test_concurrent_stress_burst() {
    std::cout << "Running test_concurrent_stress_burst..." << std::endl;
    constexpr size_t num_tasks = 1000;
    ThreadPool pool(8, true);
    std::atomic<size_t> counter{0};

    for (size_t i = 0; i < num_tasks; ++i) {
        pool.enqueue([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    pool.waitIdle();
    TEST_CHECK(counter.load() == num_tasks, "1000 burst tasks successfully processed");
}

void test_cpu_affinity_helpers() {
    std::cout << "Running test_cpu_affinity_helpers..." << std::endl;
    size_t cores = CPUAffinity::getCoreCount();
    TEST_CHECK(cores >= 1, "Detected core count is at least 1");

    bool pin_ok = CPUAffinity::pinCurrentThread(0);
    std::cout << "  [INFO] CPUAffinity::pinCurrentThread(0) result: " << pin_ok << std::endl;
    TEST_CHECK(true, "pinCurrentThread executed without crash");

    bool name_ok = CPUAffinity::setThreadName("test-worker");
    std::cout << "  [INFO] CPUAffinity::setThreadName result: " << name_ok << std::endl;
    TEST_CHECK(true, "setThreadName executed safely");
}

int main() {
    std::cout << "========================================\n";
    std::cout << " Running ThreadPool & Affinity Test Suite\n";
    std::cout << "========================================\n";

    test_pool_creation_and_size();
    test_basic_task_execution();
    test_submit_with_future();
    test_exception_isolation();
    test_concurrent_stress_burst();
    test_cpu_affinity_helpers();

    std::cout << "========================================\n";
    std::cout << " Summary: " << g_test_passed << " passed, " 
              << g_test_failed << " failed.\n";
    std::cout << "========================================\n";

    return g_test_failed == 0 ? 0 : 1;
}

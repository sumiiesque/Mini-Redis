#include "router/command_router.h"
#include "backends/locking_backend.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>

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

void test_ping_and_misc() {
    std::cout << "Running test_ping_and_misc..." << std::endl;
    backends::LockingBackend backend;
    CommandRouter router(&backend);

    Response resp = router.execute({"PING", {}});
    TEST_CHECK(resp.type == ResponseType::SimpleString && resp.value == "PONG", "PING returns PONG");

    resp = router.execute({"ping", {"hello"}});
    TEST_CHECK(resp.type == ResponseType::BulkString && resp.value == "hello", "PING hello returns bulk 'hello'");

    resp = router.execute({"ECHO", {"test-echo"}});
    TEST_CHECK(resp.type == ResponseType::BulkString && resp.value == "test-echo", "ECHO returns bulk");

    resp = router.execute({"COMMAND", {}});
    TEST_CHECK(resp.type == ResponseType::Array, "COMMAND returns Array response");

    resp = router.execute({"UNKNOWN_CMD", {}});
    TEST_CHECK(resp.type == ResponseType::Error, "Unknown command returns Error");
}

void test_set_and_get() {
    std::cout << "Running test_set_and_get..." << std::endl;
    backends::LockingBackend backend;
    CommandRouter router(&backend);

    Response resp = router.execute({"set", {"framework", "antigravity"}});
    TEST_CHECK(resp.type == ResponseType::SimpleString && resp.value == "OK", "lowercase set succeeds");

    resp = router.execute({"GET", {"framework"}});
    TEST_CHECK(resp.type == ResponseType::BulkString && resp.value == "antigravity", "GET returns stored value");

    resp = router.execute({"GET", {"missing_key"}});
    TEST_CHECK(resp.type == ResponseType::NullBulkString, "GET missing returns NullBulk");

    resp = router.execute({"SET", {"temp", "quick", "PX", "500"}});
    TEST_CHECK(resp.type == ResponseType::SimpleString && resp.value == "OK", "SET with PX succeeds");

    resp = router.execute({"SET", {"temp", "quick", "PX", "-10"}});
    TEST_CHECK(resp.type == ResponseType::Error, "SET with negative PX fails");
}

void test_del_and_expire() {
    std::cout << "Running test_del_and_expire..." << std::endl;
    backends::LockingBackend backend;
    CommandRouter router(&backend);

    router.execute({"SET", {"k1", "v1"}});
    router.execute({"SET", {"k2", "v2"}});

    Response resp = router.execute({"DEL", {"k1", "k2", "nonexistent"}});
    TEST_CHECK(resp.type == ResponseType::Integer && resp.number == 2, "DEL removes 2 existing keys");

    resp = router.execute({"GET", {"k1"}});
    TEST_CHECK(resp.type == ResponseType::NullBulkString, "k1 is not found after DEL");

    router.execute({"SET", {"exp_key", "val"}});
    resp = router.execute({"EXPIRE", {"exp_key", "10"}});
    TEST_CHECK(resp.type == ResponseType::Integer && resp.number == 1, "EXPIRE returns 1 on existing key");

    resp = router.execute({"EXPIRE", {"nonexistent_key", "10"}});
    TEST_CHECK(resp.type == ResponseType::Integer && resp.number == 0, "EXPIRE returns 0 on missing key");
}

int main() {
    std::cout << "========================================\n";
    std::cout << " Running CommandRouter Test Suite\n";
    std::cout << "========================================\n";

    test_ping_and_misc();
    test_set_and_get();
    test_del_and_expire();

    std::cout << "========================================\n";
    std::cout << " Summary: " << g_test_passed << " passed, " 
              << g_test_failed << " failed.\n";
    std::cout << "========================================\n";

    return g_test_failed == 0 ? 0 : 1;
}

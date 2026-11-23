#include "protocol/resp_parser.h"
#include "protocol/resp_encoder.h"
#include "common/command.h"
#include "common/response.h"

#include <cassert>
#include <iostream>
#include <string>
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

void test_basic_array_parsing() {
    std::cout << "Running test_basic_array_parsing..." << std::endl;
    std::string req = 
        "*3\r\n"
        "$3\r\nSET\r\n"
        "$4\r\nuser\r\n"
        "$6\r\nSmriti\r\n";

    Command cmd;
    size_t consumed = 0;
    RESPParser parser;

    bool ok = parser.parse(req, cmd, consumed);
    TEST_CHECK(ok, "Parse valid 3-arg SET command");
    TEST_CHECK(cmd.name == "SET", "Command name is SET");
    TEST_CHECK(cmd.arg.size() == 2, "Argument count is 2");
    TEST_CHECK(cmd.arg[0] == "user", "Arg 0 is 'user'");
    TEST_CHECK(cmd.arg[1] == "Smriti", "Arg 1 is 'Smriti'");
    TEST_CHECK(consumed == req.size(), "All bytes consumed");
}

void test_inline_command_parsing() {
    std::cout << "Running test_inline_command_parsing..." << std::endl;
    std::string req = "PING my-heartbeat-msg\r\n";

    Command cmd;
    size_t consumed = 0;
    RESPParser parser;

    bool ok = parser.parse(req, cmd, consumed);
    TEST_CHECK(ok, "Parse inline PING command");
    TEST_CHECK(cmd.name == "PING", "Command name is PING");
    TEST_CHECK(cmd.arg.size() == 1, "Argument count is 1");
    TEST_CHECK(cmd.arg[0] == "my-heartbeat-msg", "Argument is 'my-heartbeat-msg'");
    TEST_CHECK(consumed == req.size(), "All bytes consumed for inline");
}

void test_fragmented_packet_handling() {
    std::cout << "Running test_fragmented_packet_handling..." << std::endl;
    std::string full_req = 
        "*3\r\n"
        "$3\r\nSET\r\n"
        "$9\r\nbenchmark\r\n"
        "$10\r\ncontinuous\r\n";

    RESPParser parser;
    Command cmd;
    size_t consumed = 0;

    for (size_t len = 1; len < full_req.size(); ++len) {
        std::string partial = full_req.substr(0, len);
        bool ok = false;
        try {
            ok = parser.parse(partial, cmd, consumed);
        } catch (...) {
            TEST_CHECK(false, "Unexpected exception on partial packet");
        }
        TEST_CHECK(!ok, "Partial request should return false without error");
        TEST_CHECK(consumed == 0, "Consumed must remain 0 on incomplete frame");
    }

    bool final_ok = parser.parse(full_req, cmd, consumed);
    TEST_CHECK(final_ok, "Complete request parses once all fragments arrive");
    TEST_CHECK(cmd.name == "SET", "Final command matches");
    TEST_CHECK(cmd.arg[1] == "continuous", "Payload matches");
}

void test_pipelined_requests() {
    std::cout << "Running test_pipelined_requests..." << std::endl;
    std::string stream = 
        "*1\r\n$4\r\nPING\r\n"
        "*2\r\n$3\r\nGET\r\n$3\r\nfoo\r\n"
        "*2\r\n$3\r\nDEL\r\n$3\r\nfoo\r\n";

    RESPParser parser;
    std::vector<std::string> parsed_names;
    size_t offset = 0;

    while (offset < stream.size()) {
        std::string current_slice = stream.substr(offset);
        Command cmd;
        size_t consumed = 0;

        bool ok = parser.parse(current_slice, cmd, consumed);
        if (!ok || consumed == 0) {
            break;
        }

        parsed_names.push_back(cmd.name);
        offset += consumed;
    }

    TEST_CHECK(parsed_names.size() == 3, "All 3 pipelined commands parsed");
    TEST_CHECK(parsed_names[0] == "PING", "First pipelined cmd is PING");
    TEST_CHECK(parsed_names[1] == "GET", "Second pipelined cmd is GET");
    TEST_CHECK(parsed_names[2] == "DEL", "Third pipelined cmd is DEL");
    TEST_CHECK(offset == stream.size(), "Entire pipelined stream consumed");
}

void test_resp_encoder() {
    std::cout << "Running test_resp_encoder..." << std::endl;
    RESPEncoder encoder;

    TEST_CHECK(encoder.encode(Response::simple("OK")) == "+OK\r\n", "SimpleString format");
    TEST_CHECK(encoder.encode(Response::error("ERR syntax")) == "-ERR syntax\r\n", "Error format");
    TEST_CHECK(encoder.encode(Response::integer(42)) == ":42\r\n", "Integer format");
    TEST_CHECK(encoder.encode(Response::bulk("Hello World")) == "$11\r\nHello World\r\n", "BulkString format");
    TEST_CHECK(encoder.encode(Response::nullBulk()) == "$-1\r\n", "NullBulkString format");

    Response array_resp = Response::array({
        Response::simple("first"),
        Response::integer(100),
        Response::bulk("third")
    });
    std::string expected_arr = "*3\r\n+first\r\n:100\r\n$5\r\nthird\r\n";
    TEST_CHECK(encoder.encode(array_resp) == expected_arr, "Array response format");
}

int main() {
    std::cout << "========================================\n";
    std::cout << " Running RESP Parser & Encoder Tests\n";
    std::cout << "========================================\n";

    test_basic_array_parsing();
    test_inline_command_parsing();
    test_fragmented_packet_handling();
    test_pipelined_requests();
    test_resp_encoder();

    std::cout << "========================================\n";
    std::cout << " Summary: " << g_test_passed << " passed, " 
              << g_test_failed << " failed.\n";
    std::cout << "========================================\n";

    return g_test_failed == 0 ? 0 : 1;
}

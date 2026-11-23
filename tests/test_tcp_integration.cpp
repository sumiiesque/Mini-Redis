#include "network/tcp_server.h"
#include "router/command_router.h"
#include "backends/locking_backend.h"

#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
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

static std::string send_and_recv(int fd, const std::string& request) {
    if (send(fd, request.data(), request.size(), 0) == -1) {
        return "";
    }

    char buffer[4096];
    ssize_t n = recv(fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        return "";
    }
    buffer[n] = '\0';
    return std::string(buffer, n);
}

int main() {
    std::cout << "========================================\n";
    std::cout << " Running Mini-Redis TCP Integration Tests\n";
    std::cout << "========================================\n";

    constexpr int kTestPort = 16389;
    backends::LockingBackend backend;
    CommandRouter router(&backend);
    tcpserver server(kTestPort, &router, 4);

    std::thread server_thread([&server]() {
        server.start();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    TEST_CHECK(client_fd != -1, "Client socket creation");

    sockaddr_in saddr{};
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(kTestPort);
    inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);

    int conn_rc = connect(client_fd, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr));
    TEST_CHECK(conn_rc == 0, "TCP connection to server succeeded");

    if (conn_rc == 0) {
        std::string ping_req = "*1\r\n$4\r\nPING\r\n";
        std::string ping_resp = send_and_recv(client_fd, ping_req);
        TEST_CHECK(ping_resp == "+PONG\r\n", "PING returned +PONG\\r\\n");

        std::string set_req = "*3\r\n$3\r\nSET\r\n$6\r\nleader\r\n$6\r\nSmriti\r\n";
        std::string set_resp = send_and_recv(client_fd, set_req);
        TEST_CHECK(set_resp == "+OK\r\n", "SET returned +OK\\r\\n");

        std::string get_req = "*2\r\n$3\r\nGET\r\n$6\r\nleader\r\n";
        std::string get_resp = send_and_recv(client_fd, get_req);
        TEST_CHECK(get_resp == "$6\r\nSmriti\r\n", "GET returned $6\\r\\nSmriti\\r\\n");

        std::string px_req = "*5\r\n$3\r\nSET\r\n$8\r\nttl_test\r\n$4\r\ntemp\r\n$2\r\nPX\r\n$3\r\n150\r\n";
        std::string px_resp = send_and_recv(client_fd, px_req);
        TEST_CHECK(px_resp == "+OK\r\n", "SET with PX returned +OK\\r\\n");

        std::string get_before_req = "*2\r\n$3\r\nGET\r\n$8\r\nttl_test\r\n";
        std::string get_before_resp = send_and_recv(client_fd, get_before_req);
        TEST_CHECK(get_before_resp == "$4\r\ntemp\r\n", "GET before TTL returned value");

        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        std::string get_after_resp = send_and_recv(client_fd, get_before_req);
        TEST_CHECK(get_after_resp == "$-1\r\n", "GET after TTL returned NullBulk $-1\\r\\n");

        std::string del_req = "*2\r\n$3\r\nDEL\r\n$6\r\nleader\r\n";
        std::string del_resp = send_and_recv(client_fd, del_req);
        TEST_CHECK(del_resp == ":1\r\n", "DEL returned :1\\r\\n");

        std::string get_deleted_resp = send_and_recv(client_fd, get_req);
        TEST_CHECK(get_deleted_resp == "$-1\r\n", "GET after DEL returned $-1\\r\\n");

        close(client_fd);
    }

    std::cout << "Testing 5 concurrent client connections...\n";
    std::vector<std::thread> client_threads;
    std::atomic<int> concurrent_success{0};

    for (int i = 0; i < 5; ++i) {
        client_threads.emplace_back([i, &concurrent_success]() {
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in sa{};
            sa.sin_family = AF_INET;
            sa.sin_port = htons(kTestPort);
            inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);

            if (connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0) {
                std::string k = "concurrent_key_" + std::to_string(i);
                std::string v = "val_" + std::to_string(i);
                std::string req = "*3\r\n$3\r\nSET\r\n$" + std::to_string(k.size()) + "\r\n" + k +
                                  "\r\n$" + std::to_string(v.size()) + "\r\n" + v + "\r\n";
                std::string res = send_and_recv(fd, req);
                if (res == "+OK\r\n") {
                    concurrent_success++;
                }
            }
            close(fd);
        });
    }

    for (auto& t : client_threads) {
        if (t.joinable()) t.join();
    }
    TEST_CHECK(concurrent_success.load() == 5, "All 5 concurrent clients succeeded");

    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }

    std::cout << "========================================\n";
    std::cout << " Summary: " << g_test_passed << " passed, " 
              << g_test_failed << " failed.\n";
    std::cout << "========================================\n";

    return g_test_failed == 0 ? 0 : 1;
}

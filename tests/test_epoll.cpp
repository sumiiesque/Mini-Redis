#include "network/epoll_reactor.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

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

void test_reactor_lifecycle() {
    std::cout << "Running test_reactor_lifecycle..." << std::endl;
    EpollReactor reactor;
    TEST_CHECK(!reactor.isOpen(), "Reactor initially not open");
    TEST_CHECK(reactor.init(), "Reactor initializes successfully");
    TEST_CHECK(reactor.isOpen(), "Reactor is open after init");
    reactor.closeReactor();
    TEST_CHECK(!reactor.isOpen(), "Reactor closed properly");
}

void test_socket_options() {
    std::cout << "Running test_socket_options..." << std::endl;
    int fds[2];
    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    TEST_CHECK(rc == 0, "socketpair created");

    TEST_CHECK(EpollReactor::setNonBlocking(fds[0]), "setNonBlocking on fd0");
    TEST_CHECK(EpollReactor::setNonBlocking(fds[1]), "setNonBlocking on fd1");
    TEST_CHECK(EpollReactor::setSocketReuse(fds[0]), "setSocketReuse on fd0");

    close(fds[0]);
    close(fds[1]);
}

void test_edge_triggered_drain() {
    std::cout << "Running test_edge_triggered_drain..." << std::endl;
    int fds[2];
    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    TEST_CHECK(rc == 0, "socketpair created");

    EpollReactor::setNonBlocking(fds[0]);
    EpollReactor::setNonBlocking(fds[1]);

    std::string test_payload = "ECHO: Mini-Redis Edge-Triggered Epoll Reactor Test";
    std::string out_buf = test_payload;

    ssize_t sent = EpollReactor::drainWrite(fds[0], out_buf);
    TEST_CHECK(sent == static_cast<ssize_t>(test_payload.size()), "All bytes written via drainWrite");
    TEST_CHECK(out_buf.empty(), "Output buffer emptied");

    std::string in_buf;
    ssize_t read_bytes = EpollReactor::drainRead(fds[1], in_buf);
    TEST_CHECK(read_bytes == static_cast<ssize_t>(test_payload.size()), "All bytes read via drainRead");
    TEST_CHECK(in_buf == test_payload, "Read payload matches written payload");

    ssize_t second_read = EpollReactor::drainRead(fds[1], in_buf);
    TEST_CHECK(second_read == 0, "Second drainRead returns 0 (EAGAIN reached)");

    close(fds[0]);
    close(fds[1]);
}

void test_reactor_event_polling() {
    std::cout << "Running test_reactor_event_polling..." << std::endl;
    int fds[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    EpollReactor::setNonBlocking(fds[0]);
    EpollReactor::setNonBlocking(fds[1]);

    EpollReactor reactor;
    reactor.init();

    bool added = reactor.add(fds[1], ReactorEvents::READ | ReactorEvents::EDGE);
    TEST_CHECK(added, "Registered fd1 with READ|EDGE in reactor");

    std::string ping = "PING\r\n";
    EpollReactor::drainWrite(fds[0], ping);

    std::vector<FdEvent> fired;
    int nfds = reactor.waitEvents(fired, 500);
    TEST_CHECK(nfds >= 1, "Reactor notified of ready descriptor");

    bool found_read = false;
    for (const auto& ev : fired) {
        if (ev.fd == fds[1] && (ev.events & ReactorEvents::READ)) {
            found_read = true;
        }
    }
    TEST_CHECK(found_read, "READ event correctly fired for fd1");

    std::string response;
    EpollReactor::drainRead(fds[1], response);
    TEST_CHECK(response == "PING\r\n", "Read data matches fired event");

    reactor.remove(fds[1]);
    close(fds[0]);
    close(fds[1]);
}

int main() {
    std::cout << "========================================\n";
    std::cout << " Running EpollReactor Test Suite\n";
    std::cout << "========================================\n";

    test_reactor_lifecycle();
    test_socket_options();
    test_edge_triggered_drain();
    test_reactor_event_polling();

    std::cout << "========================================\n";
    std::cout << " Summary: " << g_test_passed << " passed, " 
              << g_test_failed << " failed.\n";
    std::cout << "========================================\n";

    return g_test_failed == 0 ? 0 : 1;
}

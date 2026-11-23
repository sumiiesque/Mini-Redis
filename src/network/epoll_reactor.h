#ifndef EPOLL_REACTOR_H
#define EPOLL_REACTOR_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <sys/types.h>

namespace ReactorEvents {
    constexpr uint32_t READ  = 0x01;
    constexpr uint32_t WRITE = 0x02;
    constexpr uint32_t EDGE  = 0x04;
    constexpr uint32_t ERROR_EV = 0x08;
    constexpr uint32_t HUP   = 0x10;
}

struct FdEvent {
    int fd;
    uint32_t events;
};

class EpollReactor {
private:
    int poll_fd;
    bool is_open;

public:
    EpollReactor();
    ~EpollReactor();

    bool init();
    bool add(int fd, uint32_t events);
    bool modify(int fd, uint32_t events);
    bool remove(int fd);
    int waitEvents(std::vector<FdEvent>& fired, int timeout_ms);
    void closeReactor();

    bool isOpen() const noexcept { return is_open; }

    static bool setNonBlocking(int fd);
    static bool setSocketReuse(int fd);
    static bool setTcpNoDelay(int fd);

    static ssize_t drainRead(int fd, std::string& buffer);
    static ssize_t drainWrite(int fd, std::string& buffer);

    EpollReactor(const EpollReactor&) = delete;
    EpollReactor& operator=(const EpollReactor&) = delete;
};

#endif

#include "epoll_reactor.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>

#if defined(__linux__)
#include <sys/epoll.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#include <sys/time.h>
#endif

EpollReactor::EpollReactor() : poll_fd(-1), is_open(false) {}

EpollReactor::~EpollReactor() {
    closeReactor();
}

bool EpollReactor::init() {
    if (is_open) {
        return true;
    }

#if defined(__linux__)
    poll_fd = epoll_create1(0);
#elif defined(__APPLE__)
    poll_fd = kqueue();
#else
    poll_fd = -1;
#endif

    if (poll_fd == -1) {
        return false;
    }

    is_open = true;
    return true;
}

void EpollReactor::closeReactor() {
    if (poll_fd != -1) {
        close(poll_fd);
        poll_fd = -1;
    }
    is_open = false;
}

bool EpollReactor::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

bool EpollReactor::setSocketReuse(int fd) {
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        return false;
    }
#if defined(SO_REUSEPORT)
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
    return true;
}

bool EpollReactor::setTcpNoDelay(int fd) {
    int opt = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == 0;
}

bool EpollReactor::add(int fd, uint32_t events) {
    if (!is_open) return false;

#if defined(__linux__)
    struct epoll_event ev{};
    ev.data.fd = fd;
    if (events & ReactorEvents::READ)  ev.events |= EPOLLIN;
    if (events & ReactorEvents::WRITE) ev.events |= EPOLLOUT;
    if (events & ReactorEvents::EDGE)  ev.events |= EPOLLET;
    ev.events |= EPOLLRDHUP | EPOLLERR;

    return epoll_ctl(poll_fd, EPOLL_CTL_ADD, fd, &ev) == 0;

#elif defined(__APPLE__)
    struct kevent change_events[2];
    int nchanges = 0;

    uint16_t flags = EV_ADD | EV_ENABLE;
    if (events & ReactorEvents::EDGE) {
        flags |= EV_CLEAR;
    }

    if (events & ReactorEvents::READ) {
        EV_SET(&change_events[nchanges++], fd, EVFILT_READ, flags, 0, 0, nullptr);
    }
    if (events & ReactorEvents::WRITE) {
        EV_SET(&change_events[nchanges++], fd, EVFILT_WRITE, flags, 0, 0, nullptr);
    }

    if (nchanges > 0) {
        return kevent(poll_fd, change_events, nchanges, nullptr, 0, nullptr) != -1;
    }
    return true;
#else
    (void)fd; (void)events;
    return false;
#endif
}

bool EpollReactor::modify(int fd, uint32_t events) {
    if (!is_open) return false;

#if defined(__linux__)
    struct epoll_event ev{};
    ev.data.fd = fd;
    if (events & ReactorEvents::READ)  ev.events |= EPOLLIN;
    if (events & ReactorEvents::WRITE) ev.events |= EPOLLOUT;
    if (events & ReactorEvents::EDGE)  ev.events |= EPOLLET;
    ev.events |= EPOLLRDHUP | EPOLLERR;

    return epoll_ctl(poll_fd, EPOLL_CTL_MOD, fd, &ev) == 0;

#elif defined(__APPLE__)
    struct kevent change_events[2];
    int nchanges = 0;

    uint16_t read_flag = (events & ReactorEvents::READ) ? (EV_ADD | EV_ENABLE) : EV_DELETE;
    uint16_t write_flag = (events & ReactorEvents::WRITE) ? (EV_ADD | EV_ENABLE) : EV_DELETE;

    if (events & ReactorEvents::EDGE) {
        if (events & ReactorEvents::READ) read_flag |= EV_CLEAR;
        if (events & ReactorEvents::WRITE) write_flag |= EV_CLEAR;
    }

    EV_SET(&change_events[nchanges++], fd, EVFILT_READ, read_flag, 0, 0, nullptr);
    EV_SET(&change_events[nchanges++], fd, EVFILT_WRITE, write_flag, 0, 0, nullptr);

    kevent(poll_fd, change_events, nchanges, nullptr, 0, nullptr);
    return true;
#else
    (void)fd; (void)events;
    return false;
#endif
}

bool EpollReactor::remove(int fd) {
    if (!is_open) return false;

#if defined(__linux__)
    return epoll_ctl(poll_fd, EPOLL_CTL_DEL, fd, nullptr) == 0;
#elif defined(__APPLE__)
    struct kevent change_events[2];
    EV_SET(&change_events[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&change_events[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    kevent(poll_fd, change_events, 2, nullptr, 0, nullptr);
    return true;
#else
    (void)fd;
    return false;
#endif
}

int EpollReactor::waitEvents(std::vector<FdEvent>& fired, int timeout_ms) {
    if (!is_open) return -1;
    fired.clear();

#if defined(__linux__)
    struct epoll_event evs[64];
    int n = epoll_wait(poll_fd, evs, 64, timeout_ms);
    if (n < 0) {
        if (errno == EINTR) return 0;
        return -1;
    }

    for (int i = 0; i < n; ++i) {
        FdEvent fe{};
        fe.fd = evs[i].data.fd;
        if (evs[i].events & EPOLLIN)  fe.events |= ReactorEvents::READ;
        if (evs[i].events & EPOLLOUT) fe.events |= ReactorEvents::WRITE;
        if (evs[i].events & (EPOLLERR | EPOLLRDHUP)) fe.events |= ReactorEvents::ERROR_EV;
        if (evs[i].events & EPOLLHUP) fe.events |= ReactorEvents::HUP;
        fired.push_back(fe);
    }
    return n;

#elif defined(__APPLE__)
    struct kevent evs[64];
    struct timespec ts{};
    struct timespec* pts = nullptr;

    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000;
        pts = &ts;
    }

    int n = kevent(poll_fd, nullptr, 0, evs, 64, pts);
    if (n < 0) {
        if (errno == EINTR) return 0;
        return -1;
    }

    for (int i = 0; i < n; ++i) {
        FdEvent fe{};
        fe.fd = static_cast<int>(evs[i].ident);
        if (evs[i].filter == EVFILT_READ)  fe.events |= ReactorEvents::READ;
        if (evs[i].filter == EVFILT_WRITE) fe.events |= ReactorEvents::WRITE;
        if (evs[i].flags & (EV_ERROR | EV_EOF)) fe.events |= ReactorEvents::HUP;
        fired.push_back(fe);
    }
    return n;
#else
    (void)timeout_ms;
    return -1;
#endif
}

ssize_t EpollReactor::drainRead(int fd, std::string& buffer) {
    char chunk[4096];
    ssize_t total_read = 0;

    while (true) {
        ssize_t bytes = recv(fd, chunk, sizeof(chunk), 0);
        if (bytes > 0) {
            buffer.append(chunk, static_cast<size_t>(bytes));
            total_read += bytes;
        } else if (bytes == 0) {
            if (total_read > 0) {
                return total_read;
            }
            return 0;
        } else {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return -1;
        }
    }

    return total_read;
}

ssize_t EpollReactor::drainWrite(int fd, std::string& buffer) {
    if (buffer.empty()) {
        return 0;
    }

    size_t sent_total = 0;
    while (sent_total < buffer.size()) {
        ssize_t sent = send(fd, buffer.data() + sent_total, buffer.size() - sent_total, 0);
        if (sent > 0) {
            sent_total += static_cast<size_t>(sent);
        } else if (sent == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return -1;
        }
    }

    buffer.erase(0, sent_total);
    return static_cast<ssize_t>(sent_total);
}

#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include "../router/command_router.h"
#include "../protocol/resp_parser.h"
#include "../protocol/resp_encoder.h"
#include "../thread/thread_pool.h"
#include "epoll_reactor.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

class tcpserver {
private:
    struct ClientSession {
        int fd;
        std::string in_buf;
        std::string out_buf;
        std::mutex session_mutex;
        bool write_registered = false;
        bool close_after_write = false;

        explicit ClientSession(int fd) : fd(fd) {}
    };

    int port;
    int server_fd;
    size_t worker_threads;
    CommandRouter* router;

    std::atomic<bool> is_running;
    EpollReactor reactor;
    std::unique_ptr<ThreadPool> worker_pool;

    std::mutex clients_mutex;
    std::unordered_map<int, std::shared_ptr<ClientSession>> clients;

    RESPParser parser;
    RESPEncoder encoder;

    void handleAccept();
    void handleRead(int client_fd);
    void handleWrite(int client_fd);
    void closeClient(int client_fd);
    void queueResponse(int client_fd, const std::string& data, bool close_after = false);

public:
    explicit tcpserver(int port, CommandRouter* router, size_t worker_threads = 4);
    ~tcpserver();

    tcpserver(const tcpserver&) = delete;
    tcpserver& operator=(const tcpserver&) = delete;

    void start();
    void stop();
    int getPort() const noexcept { return port; }
    bool isRunning() const noexcept { return is_running.load(); }
};

#endif

#include "tcp_server.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

tcpserver::tcpserver(int port, CommandRouter* router, size_t worker_threads)
    : port(port),
      server_fd(-1),
      worker_threads(worker_threads),
      router(router),
      is_running(false) {}

tcpserver::~tcpserver() {
    stop();
}

void tcpserver::stop() {
    bool expected = true;
    if (is_running.compare_exchange_strong(expected, false)) {
        if (server_fd != -1) {
            reactor.remove(server_fd);
            close(server_fd);
            server_fd = -1;
        }

        std::vector<int> client_fds;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (const auto& pair : clients) {
                client_fds.push_back(pair.first);
            }
        }

        for (int fd : client_fds) {
            closeClient(fd);
        }

        reactor.closeReactor();
        if (worker_pool) {
            worker_pool->shutdown();
        }
    }
}

void tcpserver::closeClient(int client_fd) {
    reactor.remove(client_fd);
    close(client_fd);

    std::lock_guard<std::mutex> lock(clients_mutex);
    clients.erase(client_fd);
}

void tcpserver::handleAccept() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);

        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                break;
            }
            break;
        }

        EpollReactor::setNonBlocking(client_fd);
        EpollReactor::setTcpNoDelay(client_fd);

        auto session = std::make_shared<ClientSession>(client_fd);
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients[client_fd] = session;
        }

        if (!reactor.add(client_fd, ReactorEvents::READ | ReactorEvents::EDGE)) {
            closeClient(client_fd);
        }
    }
}

void tcpserver::queueResponse(int client_fd, const std::string& data, bool close_after) {
    std::shared_ptr<ClientSession> session;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = clients.find(client_fd);
        if (it == clients.end()) {
            return;
        }
        session = it->second;
    }

    std::lock_guard<std::mutex> lock(session->session_mutex);
    session->out_buf += data;
    if (close_after) {
        session->close_after_write = true;
    }

    ssize_t sent = EpollReactor::drainWrite(session->fd, session->out_buf);
    if (sent < 0) {
        closeClient(client_fd);
        return;
    }

    if (session->out_buf.empty()) {
        if (session->close_after_write) {
            closeClient(client_fd);
        } else if (session->write_registered) {
            reactor.modify(session->fd, ReactorEvents::READ | ReactorEvents::EDGE);
            session->write_registered = false;
        }
    } else {
        if (!session->write_registered) {
            reactor.modify(session->fd, ReactorEvents::READ | ReactorEvents::WRITE | ReactorEvents::EDGE);
            session->write_registered = true;
        }
    }
}

void tcpserver::handleWrite(int client_fd) {
    std::shared_ptr<ClientSession> session;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = clients.find(client_fd);
        if (it == clients.end()) return;
        session = it->second;
    }

    std::lock_guard<std::mutex> lock(session->session_mutex);
    ssize_t sent = EpollReactor::drainWrite(session->fd, session->out_buf);
    if (sent < 0) {
        closeClient(client_fd);
        return;
    }

    if (session->out_buf.empty()) {
        if (session->close_after_write) {
            closeClient(client_fd);
        } else if (session->write_registered) {
            reactor.modify(session->fd, ReactorEvents::READ | ReactorEvents::EDGE);
            session->write_registered = false;
        }
    }
}

void tcpserver::handleRead(int client_fd) {
    std::shared_ptr<ClientSession> session;
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = clients.find(client_fd);
        if (it == clients.end()) return;
        session = it->second;
    }

    std::lock_guard<std::mutex> lock(session->session_mutex);
    ssize_t bytes_read = EpollReactor::drainRead(session->fd, session->in_buf);
    if (bytes_read <= 0 && session->in_buf.empty()) {
        closeClient(client_fd);
        return;
    }

    size_t offset = 0;
    while (offset < session->in_buf.size()) {
        Command cmd;
        size_t consumed = 0;
        std::string request_slice = session->in_buf.substr(offset);

        try {
            if (!parser.parse(request_slice, cmd, consumed)) {
                break;
            }

            if (consumed == 0) break;
            offset += consumed;

            if (worker_pool) {
                worker_pool->enqueue([this, client_fd, cmd]() {
                    Response response = this->router->execute(cmd);
                    std::string encoded = this->encoder.encode(response);
                    this->queueResponse(client_fd, encoded);
                });
            } else {
                Response response = router->execute(cmd);
                std::string encoded = encoder.encode(response);
                queueResponse(client_fd, encoded);
            }
        } catch (const std::exception& ex) {
            std::string err_resp = encoder.encode(Response::error(std::string("ERR ") + ex.what()));
            queueResponse(client_fd, err_resp, true);
            offset = session->in_buf.size();
            break;
        }
    }

    if (offset > 0) {
        session->in_buf.erase(0, offset);
    }
}

void tcpserver::start() {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "tcpserver: socket() creation failed: " << strerror(errno) << '\n';
        return;
    }

    EpollReactor::setSocketReuse(server_fd);
    EpollReactor::setNonBlocking(server_fd);

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == -1) {
        std::cerr << "tcpserver: bind() failed on port " << port << ": " << strerror(errno) << '\n';
        close(server_fd);
        server_fd = -1;
        return;
    }

    if (listen(server_fd, 512) == -1) {
        std::cerr << "tcpserver: listen() failed: " << strerror(errno) << '\n';
        close(server_fd);
        server_fd = -1;
        return;
    }

    if (!reactor.init()) {
        std::cerr << "tcpserver: reactor initialization failed\n";
        close(server_fd);
        server_fd = -1;
        return;
    }

    if (!reactor.add(server_fd, ReactorEvents::READ | ReactorEvents::EDGE)) {
        std::cerr << "tcpserver: failed to add server socket to reactor\n";
        close(server_fd);
        server_fd = -1;
        return;
    }

    worker_pool = std::make_unique<ThreadPool>(worker_threads, true);
    is_running = true;

    std::cout << "Mini-Redis edge-triggered reactor running on port " << port
              << " with " << worker_threads << " pinned pthread workers.\n";

    std::vector<FdEvent> fired_events;
    while (is_running.load()) {
        int n = reactor.waitEvents(fired_events, 200);
        if (n < 0 && is_running.load()) {
            std::cerr << "tcpserver: reactor poll error: " << strerror(errno) << '\n';
            break;
        }

        for (const auto& ev : fired_events) {
            if (ev.fd == server_fd) {
                handleAccept();
            } else {
                if (ev.events & ReactorEvents::READ) {
                    handleRead(ev.fd);
                }
                if (ev.events & ReactorEvents::WRITE) {
                    handleWrite(ev.fd);
                }
                if (ev.events & (ReactorEvents::ERROR_EV | ReactorEvents::HUP)) {
                    closeClient(ev.fd);
                }
            }
        }
    }

    stop();
}

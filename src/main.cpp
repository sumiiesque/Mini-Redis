#include "network/tcp_server.h"
#include "backends/locking_backend.h"
#include "backends/mvcc_backend.h"

#include <csignal>
#include <iostream>
#include <memory>
#include <string>

static tcpserver* g_server = nullptr;

static void handle_signal(int sig) {
    if (g_server != nullptr) {
        std::cout << "\nCaught signal " << sig << ", gracefully shutting down Mini-Redis...\n";
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    std::string backend_name = "locking";
    int port = 6379;
    size_t worker_threads = 4;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--backend" && i + 1 < argc) {
            backend_name = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            worker_threads = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Mini-Redis: Multi-Threaded In-Memory Key-Value Store\n"
                      << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --backend <locking|mvcc>   Select concurrency backend (default: locking)\n"
                      << "  --port <port>             TCP port to bind (default: 6379)\n"
                      << "  --threads <n>             Worker pool thread count (default: 4)\n"
                      << "  --help, -h                Display this help message\n";
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\nRun with --help for usage.\n";
            return 1;
        }
    }

    std::unique_ptr<KVBackend> backend;
    if (backend_name == "locking") {
        backend = std::make_unique<backends::LockingBackend>();
    } else if (backend_name == "mvcc") {
        backend = std::make_unique<backends::MVCCBackend>();
    } else {
        std::cerr << "Unknown backend: " << backend_name << '\n';
        std::cerr << "Supported backends: locking, mvcc\n";
        return 1;
    }

    std::cout << "========================================================\n"
              << " Mini-Redis Multi-Threaded Engine\n"
              << " Backend: " << backend_name << "\n"
              << " Port: " << port << " | Workers: " << worker_threads << "\n"
              << "========================================================\n";

    CommandRouter router(backend.get());
    tcpserver server(port, &router, worker_threads);
    g_server = &server;

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    server.start();
    g_server = nullptr;

    return 0;
}

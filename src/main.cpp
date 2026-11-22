#include "network/tcp_server.h"
#include "backends/locking_backend.h"
#include "backends/mvcc_backend.h"

#include <iostream>
#include <memory>
#include <string>

int main(int argc, char* argv[]) {
    std::string backend_name = "locking";

    if (argc == 3 && std::string(argv[1]) == "--backend") {
        backend_name = argv[2];
    } else if (argc != 1) {
        std::cerr << "Usage: " << argv[0]
                  << " [--backend locking|mvcc]\n";
        return 1;
    }

    std::unique_ptr<KVBackend> backend;

    if (backend_name == "locking") {
        backend = std::make_unique<backends::LockingBackend>();
    } else if (backend_name == "mvcc") {
        backend = std::make_unique<backends::MVCCBackend>();
    } else {
        std::cerr << "Unknown backend: " << backend_name << '\n';
        std::cerr << "Choose locking or mvcc.\n";
        return 1;
    }

    CommandRouter router(backend.get());
    tcpserver server(6379, &router);

    std::cout << "Using backend: " << backend_name << '\n';
    server.start();

    return 0;
}
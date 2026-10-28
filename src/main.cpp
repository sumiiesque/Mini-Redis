#include "network/tcp_server.h"

int main() {
    tcpserver server(6379);
    server.start();
    return 0;
}
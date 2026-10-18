
#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include "../router/command_router.h"

class tcpserver {
private:
    int server_fd;
    int port;
    CommandRouter* router;

public:
    tcpserver(int port, CommandRouter* router);
    ~tcpserver();

    tcpserver(const tcpserver&) = delete;
    tcpserver& operator=(const tcpserver&) = delete;

    void start();
};

#endif

#ifndef TCP_SERVER_H
#define TCP_SERVER_H

class tcpserver{
    private:
    int server_fd;
    int port;
    public:
    tcpserver(int port);
    ~tcpserver();

    void start();
};

#endif
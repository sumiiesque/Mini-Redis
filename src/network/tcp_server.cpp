#include "tcp_server.h"

#include<iostream>
#include<cstring>
#include<unistd.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<fcntl.h>
#include<sys/event.h>

using namespace std;

tcpserver::tcpserver(int port){
    this->port=port;
    this->server_fd=-1;
}

tcpserver::~tcpserver(){
    if(server_fd!=-1)
    close(server_fd);
}

void tcpserver::start(){
    //socket created
    server_fd=socket(AF_INET,SOCK_STREAM,0);

    if(server_fd==-1){
        cerr<<"failed to create socket\n";
        return;
    }

    cout<<"socket created\n";

    sockaddr_in server_add{};
    server_add.sin_family=AF_INET;
    server_add.sin_addr.s_addr=INADDR_ANY;
    server_add.sin_port=htons(port);

    if(::bind(server_fd,(struct sockaddr*)&server_add,sizeof(server_add)) == -1){
        cerr<<"Bind failed\n";
        return;
    }

    cout<<"socket bound to port "<<port<<"\n";

    if(listen(server_fd,10)==-1){
        cerr<<"listen failed\n";
        return;
    }

    cout<<"server listening on port "<<port<<"\n";

    int kqueue_fd = kqueue();
    if (kqueue_fd == -1) {
        cerr << "Failed to create kqueue\n";
        return;
    }

    struct kevent eve;

    EV_SET(&eve,server_fd,EVFILT_READ,EV_ADD,0,0,nullptr);

    if (kevent(kqueue_fd, &eve, 1, nullptr, 0, nullptr) == -1) {
        cerr << "failed to add server socket to kqueue\n";
        return;
    }

    struct kevent events[10];

    while (true) {
        int count = kevent(kqueue_fd,nullptr,0,events,10,nullptr);

        if (count == -1) {
            cerr << "kevent failed\n";
            return;
        }

        for (int i = 0; i < count; i++) {
            int fd = static_cast<int>(events[i].ident);
            if (fd == server_fd) {
                int client_fd = accept(server_fd, nullptr, nullptr);
            if (client_fd == -1) {
                cerr << "accept failed\n";
                continue;
            }
            cout << "client connected!\n";

            int flags = fcntl(client_fd, F_GETFL, 0);
            fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

            EV_SET(&eve,client_fd,EVFILT_READ,EV_ADD,0,0,nullptr);

            if (kevent(kqueue_fd, &eve, 1, nullptr, 0, nullptr) == -1) {
                cerr << "failed to add client to kqueue\n";
                close(client_fd);
            }
        }

        else if (events[i].filter == EVFILT_READ) {
            char msg[1024];
            int received = recv(fd,msg,sizeof(msg) - 1,0);

            if (received > 0) {
                msg[received] = '\0';
                cout << "Received: " << msg << endl;
            }

            else if (received == 0) {
                cout << "client disconnected\n";
                close(fd);
            }

            else {
                cerr << "recv failed\n";
                close(fd);
            }
        }
    }
}

}
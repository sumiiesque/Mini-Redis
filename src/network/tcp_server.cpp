#include "tcp_server.h"

#include<iostream>
#include<cstring>
#include<unistd.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<fcntl.h>
#include <unordered_set>
#include<sys/event.h>
#include "../protocol/resp_parser.h"
#include "../protocol/resp_encoder.h"
#include "../common/command.h"
#include "../common/response.h"
#include <unordered_map>
#include <string>
#include <cerrno>

using namespace std;

tcpserver::tcpserver(int port,CommandRouter* router){
    this->port=port;
    this->server_fd=-1;
    this->router=router;
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

    std::unordered_map<int, std::string> input_buffers;
    std::unordered_map<int, std::string> output_buffers;
    std::unordered_set<int> write_enabled;
    std::unordered_set<int> close_after_write;
    RESPParser parser;
    RESPEncoder encoder;
    struct kevent events[10];

    auto close_client = [&](int client_fd) {
    EV_SET(&eve, client_fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    kevent(kqueue_fd, &eve, 1, nullptr, 0, nullptr);

    EV_SET(&eve, client_fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    kevent(kqueue_fd, &eve, 1, nullptr, 0, nullptr);

    close(client_fd);
    input_buffers.erase(client_fd);
    output_buffers.erase(client_fd);
    write_enabled.erase(client_fd);
    close_after_write.erase(client_fd);
};

    auto queue_response = [&](int client_fd, const std::string& response) {
        output_buffers[client_fd] += response;

        if (write_enabled.insert(client_fd).second) {
            EV_SET(&eve, client_fd, EVFILT_WRITE, EV_ADD, 0, 0, nullptr);

            if (kevent(kqueue_fd, &eve, 1, nullptr, 0, nullptr) == -1) {
                close_client(client_fd);
                return false;
            }
        }

        return true;
    };

    auto flush_output = [&](int client_fd) {
        auto it = output_buffers.find(client_fd);

        if (it == output_buffers.end()) {
            return true;
        }

        std::string& pending = it->second;
        size_t sent_total = 0;

        while (sent_total < pending.size()) {
            ssize_t sent = send(
                client_fd,
                pending.data() + sent_total,
                pending.size() - sent_total,
                0
            );

            if (sent > 0) {
                sent_total += static_cast<size_t>(sent);
            } else if (sent == -1 && errno == EINTR) {
                continue;
            } else if (sent == -1 &&
                    (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            } else {
                close_client(client_fd);
                return false;
        }
        }

        pending.erase(0, sent_total);

        if (pending.empty()) {
        output_buffers.erase(client_fd);
        write_enabled.erase(client_fd);

        EV_SET(&eve, client_fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);

        if (kevent(kqueue_fd, &eve, 1, nullptr, 0, nullptr) == -1 &&
            errno != ENOENT) {
            close_client(client_fd);
            return false;
        }

        if (close_after_write.count(client_fd)) {
            close_client(client_fd);
            return false;
        }
    }
    return true;
    };

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
            input_buffers[client_fd] = "";

            int flags = fcntl(client_fd, F_GETFL, 0);
            fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

            EV_SET(&eve,client_fd,EVFILT_READ,EV_ADD,0,0,nullptr);

            if (kevent(kqueue_fd, &eve, 1, nullptr, 0, nullptr) == -1) {
                cerr << "failed to add client to kqueue\n";
                close_client(client_fd);
                continue;
                }
            }


    else if (events[i].filter == EVFILT_READ) {
        char msg[4096];
        int received = recv(fd, msg, sizeof(msg), 0);

        if (received > 0) {
            input_buffers[fd].append(msg, received);

            std::string& pending = input_buffers[fd];
            size_t consumed_total = 0;

            while (consumed_total < pending.size()) {
                Command command;
                size_t consumed = 0;

                try {
                std::string request = pending.substr(consumed_total);

                    if (!parser.parse(request, command, consumed)) {
                        break;  //Wait
                    }

                    if (consumed == 0) break;

                    Response response = router->execute(command);
                    std::string encoded = encoder.encode(response);

                    if (!queue_response(fd, encoded)) {
                        break;
                    }
                    if (!flush_output(fd)) {
                        break;
                    }
                    consumed_total += consumed;
                }
                catch (const std::exception& ex) {
                    std::string error = encoder.encode(
                        Response::error(std::string("ERR ") + ex.what())
                    );
                    if (queue_response(fd, error)) {
                        close_after_write.insert(fd);
                    } break;
                }
            }

            auto it = input_buffers.find(fd);

            if (it != input_buffers.end() && consumed_total > 0) {
                it->second.erase(0, consumed_total);
            }
            } else if (received == 0) {
                close_client(fd);
                continue;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK &&
           errno != EINTR) {
            cerr << "recv failed\n";
            close_client(fd);
            continue;
            }
        }

    else if (events[i].filter == EVFILT_WRITE) {
    if (!flush_output(fd)) {
        continue;
    }
}

   }}}
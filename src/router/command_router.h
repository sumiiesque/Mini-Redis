#ifndef COMMAND_ROUTER_H
#define COMMAND_ROUTER_H

#include "../common/command.h"
#include "../common/response.h"
#include "../../include/kv_backend.h"

#include <string>

class CommandRouter {
private:
    KVBackend* backend;

public:
    explicit CommandRouter(KVBackend* backend);

    Response execute(const Command& command);
    void setBackend(KVBackend* new_backend) noexcept { backend = new_backend; }
    KVBackend* getBackend() const noexcept { return backend; }
};

#endif

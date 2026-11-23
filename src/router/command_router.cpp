#include "command_router.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>

CommandRouter::CommandRouter(KVBackend* backend)
    : backend(backend) {}

Response CommandRouter::execute(const Command& command) {
    if (command.name.empty()) {
        return Response::error("ERR empty command");
    }

    std::string name = command.name;
    std::transform(name.begin(), name.end(), name.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });

    const auto& args = command.arg;

    if (name == "PING") {
        if (args.empty()) {
            return Response::simple("PONG");
        }
        if (args.size() == 1) {
            return Response::bulk(args[0]);
        }
        return Response::error("ERR wrong number of arguments for 'ping' command");
    }

    if (name == "ECHO") {
        if (args.size() != 1) {
            return Response::error("ERR wrong number of arguments for 'echo' command");
        }
        return Response::bulk(args[0]);
    }

    if (name == "QUIT") {
        return Response::simple("OK");
    }

    if (name == "COMMAND") {
        return Response::array({});
    }

    if (name == "INFO") {
        std::string info_str = 
            "# Server\r\n"
            "redis_version:mini-redis-1.0.0\r\n"
            "os:Darwin/Linux\r\n"
            "multiplexing_api:epoll/kqueue\r\n"
            "concurrency_control:locking/mvcc\r\n";
        return Response::bulk(info_str);
    }

    if (backend == nullptr) {
        return Response::error("ERR storage backend not configured");
    }

    if (name == "SET") {
        if (args.size() != 2 && args.size() != 4) {
            return Response::error("ERR syntax error for 'set' command");
        }

        int64_t ttl_ms = -1;

        if (args.size() == 4) {
            std::string option = args[2];
            std::transform(option.begin(), option.end(), option.begin(),
                [](unsigned char c) {
                    return static_cast<char>(std::toupper(c));
                });

            if (option != "PX") {
                return Response::error("ERR only the PX expiration option is supported");
            }

            try {
                size_t used = 0;
                long long parsed = std::stoll(args[3], &used);
                if (used != args[3].size() || parsed <= 0) {
                    return Response::error("ERR PX milliseconds must be a positive integer");
                }
                ttl_ms = parsed;
            } catch (...) {
                return Response::error("ERR invalid PX milliseconds value");
            }
        }

        KVResult result = backend->Set(args[0], args[1], ttl_ms);
        if (result.status == KVStatus::kSetOk) {
            return Response::simple("OK");
        }
        return Response::error("ERR SET operation failed");
    }

    if (name == "GET") {
        if (args.size() != 1) {
            return Response::error("ERR wrong number of arguments for 'get' command");
        }

        KVResult result = backend->Get(args[0]);
        if (result.status == KVStatus::kGetFound) {
            return Response::bulk(result.value);
        }
        if (result.status == KVStatus::kGetNotFound) {
            return Response::nullBulk();
        }
        return Response::error("ERR GET operation failed");
    }

    if (name == "DEL") {
        if (args.empty()) {
            return Response::error("ERR wrong number of arguments for 'del' command");
        }

        long long deleted = 0;
        for (const auto& key : args) {
            KVResult result = backend->Del(key);
            if (result.status == KVStatus::kDelOk) {
                ++deleted;
            } else if (result.status != KVStatus::kDelNotFound) {
                return Response::error("ERR DEL operation failed");
            }
        }
        return Response::integer(deleted);
    }

    if (name == "EXPIRE") {
        if (args.size() != 2) {
            return Response::error("ERR wrong number of arguments for 'expire' command");
        }

        try {
            size_t used = 0;
            long long seconds = std::stoll(args[1], &used);
            if (used != args[1].size()) {
                return Response::error("ERR invalid expire time");
            }
            if (seconds <= 0) {
                return Response::error("ERR expire time must be positive");
            }
            if (seconds > std::numeric_limits<int64_t>::max() / 1000) {
                return Response::error("ERR expire time too large");
            }

            KVResult result = backend->Expire(args[0], seconds * 1000);
            if (result.status == KVStatus::kExpireOk) {
                return Response::integer(1);
            }
            if (result.status == KVStatus::kExpireNotFound) {
                return Response::integer(0);
            }
            return Response::error("ERR EXPIRE operation failed");
        } catch (...) {
            return Response::error("ERR invalid expire time");
        }
    }

    return Response::error("ERR unknown command '" + command.name + "'");
}

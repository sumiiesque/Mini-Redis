
#include "resp_encoder.h"

std::string RESPEncoder::encode(const Response& response) {
    switch (response.type) {
        case ResponseType::SimpleString:
            return "+" + response.value + "\r\n";

        case ResponseType::Error:
            return "-" + response.value + "\r\n";

        case ResponseType::Integer:
            return ":" + std::to_string(response.number) + "\r\n";

        case ResponseType::BulkString:
            return "$" + std::to_string(response.value.size())
                   + "\r\n" + response.value + "\r\n";

        case ResponseType::NullBulkString:
            return "$-1\r\n";
    }

    return "-ERR unknown response type\r\n";
}

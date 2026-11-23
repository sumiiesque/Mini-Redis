#include "resp_encoder.h"

std::string RESPEncoder::encodeSimpleString(const std::string& str) {
    return "+" + str + "\r\n";
}

std::string RESPEncoder::encodeError(const std::string& err) {
    return "-" + err + "\r\n";
}

std::string RESPEncoder::encodeInteger(long long val) {
    return ":" + std::to_string(val) + "\r\n";
}

std::string RESPEncoder::encodeBulkString(const std::string& str) {
    return "$" + std::to_string(str.size()) + "\r\n" + str + "\r\n";
}

std::string RESPEncoder::encodeNullBulk() {
    return "$-1\r\n";
}

std::string RESPEncoder::encode(const Response& response) {
    switch (response.type) {
        case ResponseType::SimpleString:
            return encodeSimpleString(response.value);

        case ResponseType::Error:
            return encodeError(response.value);

        case ResponseType::Integer:
            return encodeInteger(response.number);

        case ResponseType::BulkString:
            return encodeBulkString(response.value);

        case ResponseType::NullBulkString:
            return encodeNullBulk();

        case ResponseType::Array: {
            std::string out = "*" + std::to_string(response.elements.size()) + "\r\n";
            for (const auto& elem : response.elements) {
                out += encode(elem);
            }
            return out;
        }
    }

    return "-ERR unknown response type\r\n";
}

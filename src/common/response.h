
#ifndef RESPONSE_H
#define RESPONSE_H

#include <string>

enum class ResponseType {
    SimpleString,
    Error,
    Integer,
    BulkString,
    NullBulkString
};

struct Response {
    ResponseType type;
    std::string value;
    long long number = 0;

    static Response simple(const std::string& value) {
        return {ResponseType::SimpleString, value, 0};
    }

    static Response error(const std::string& value) {
        return {ResponseType::Error, value, 0};
    }

    static Response integer(long long number) {
        return {ResponseType::Integer, "", number};
    }

    static Response bulk(const std::string& value) {
        return {ResponseType::BulkString, value, 0};
    }

    static Response nullBulk() {
        return {ResponseType::NullBulkString, "", 0};
    }
};

#endif

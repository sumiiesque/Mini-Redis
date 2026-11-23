#ifndef RESPONSE_H
#define RESPONSE_H

#include <string>
#include <vector>

enum class ResponseType {
    SimpleString,
    Error,
    Integer,
    BulkString,
    NullBulkString,
    Array
};

struct Response {
    ResponseType type;
    std::string value;
    long long number = 0;
    std::vector<Response> elements;

    static Response simple(const std::string& value) {
        return {ResponseType::SimpleString, value, 0, {}};
    }

    static Response error(const std::string& value) {
        return {ResponseType::Error, value, 0, {}};
    }

    static Response integer(long long number) {
        return {ResponseType::Integer, "", number, {}};
    }

    static Response bulk(const std::string& value) {
        return {ResponseType::BulkString, value, 0, {}};
    }

    static Response nullBulk() {
        return {ResponseType::NullBulkString, "", 0, {}};
    }

    static Response array(std::vector<Response> elements) {
        Response r{ResponseType::Array, "", 0, std::move(elements)};
        return r;
    }
};

#endif

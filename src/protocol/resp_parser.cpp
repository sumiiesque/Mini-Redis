#include "resp_parser.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

bool RESPParser::parseInline(const std::string& request,
                             Command& command,
                             size_t& consumed) {
    size_t end = request.find("\r\n");
    if (end == std::string::npos) {
        return false;
    }

    std::string line = request.substr(0, end);
    consumed = end + 2;

    std::stringstream ss(line);
    std::string token;
    Command parsed;

    while (ss >> token) {
        if (parsed.name.empty()) {
            parsed.name = token;
        } else {
            parsed.arg.push_back(token);
        }
    }

    if (parsed.name.empty()) {
        return false;
    }

    command = std::move(parsed);
    return true;
}

bool RESPParser::parse(const std::string& request,
                       Command& command,
                       size_t& consumed) {
    consumed = 0;
    if (request.empty()) {
        return false;
    }

    if (request[0] != '*') {
        return parseInline(request, command, consumed);
    }

    size_t pos = 0;
    auto readLine = [&](std::string& result) -> bool {
        size_t end = request.find("\r\n", pos);
        if (end == std::string::npos) {
            return false;
        }
        result = request.substr(pos, end - pos);
        pos = end + 2;
        return true;
    };

    std::string line;
    if (!readLine(line)) {
        return false;
    }

    if (line.empty() || line[0] != '*') {
        throw std::runtime_error("Invalid RESP array header");
    }

    int argcount = 0;
    try {
        argcount = std::stoi(line.substr(1));
    } catch (...) {
        throw std::runtime_error("Invalid RESP argument count");
    }

    if (argcount < 1) {
        throw std::runtime_error("Invalid argument count: expected at least 1");
    }

    Command parsed;
    for (int i = 0; i < argcount; ++i) {
        if (!readLine(line)) {
            return false;
        }

        if (line.empty() || line[0] != '$') {
            throw std::runtime_error("Invalid RESP bulk string header");
        }

        int len = 0;
        try {
            len = std::stoi(line.substr(1));
        } catch (...) {
            throw std::runtime_error("Invalid bulk string length integer");
        }

        if (len < 0) {
            throw std::runtime_error("Negative bulk string length not permitted in commands");
        }

        if (request.size() - pos < static_cast<size_t>(len) + 2) {
            return false;
        }

        std::string value = request.substr(pos, len);
        pos += len;

        if (request.compare(pos, 2, "\r\n") != 0) {
            throw std::runtime_error("Invalid RESP string terminator");
        }
        pos += 2;

        if (i == 0) {
            parsed.name = std::move(value);
        } else {
            parsed.arg.push_back(std::move(value));
        }
    }

    command = std::move(parsed);
    consumed = pos;
    return true;
}

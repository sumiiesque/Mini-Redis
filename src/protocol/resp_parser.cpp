#include<iostream>
#include "resp_parser.h"
#include <stdexcept>
#include <string>

using namespace std;

bool RESPParser::parse(const string& request,
                       Command& command,
                       size_t& consumed) {
    consumed = 0;
    size_t pos = 0;
    string line;

    auto readLine = [&](string& result) -> bool {
        size_t end = request.find("\r\n", pos);
        if (end == string::npos) {
            return false;
        }
        result = request.substr(pos, end - pos);
        pos = end + 2;
        return true;
    };

    if (!readLine(line)) {
        return false;
    }
    if (line.empty() || line[0] != '*') {
        throw runtime_error("Invalid RESP array");
    }
    int argcount = stoi(line.substr(1));
    if (argcount < 1) {
        throw runtime_error("Invalid argument count");
    }
    Command parsed;

    for (int i = 0; i < argcount; i++) {
        if (!readLine(line)) {
            return false;
        }
        if (line.empty() || line[0] != '$') {
            throw runtime_error("Invalid RESP bulk string");
        }
        int len = stoi(line.substr(1));
        if (len < 0) {
            throw runtime_error("Invalid bulk string length");
        }
        if (request.size() - pos < static_cast<size_t>(len) + 2) {
            return false;
        }
        string value = request.substr(pos, len);
        pos += len;
        if (request.compare(pos, 2, "\r\n") != 0) {
            throw runtime_error("Invalid RESP string terminator");
        }
        pos += 2;
        if (i == 0) {
            parsed.name = value;
        } else {
            parsed.arg.push_back(value);
        }
    }
    command = parsed;
    consumed = pos;
    return true;
}
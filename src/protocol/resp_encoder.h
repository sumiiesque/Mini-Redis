#ifndef RESP_ENCODER_H
#define RESP_ENCODER_H

#include <string>
#include <vector>
#include "../common/response.h"

class RESPEncoder {
public:
    std::string encode(const Response& response);

    static std::string encodeSimpleString(const std::string& str);
    static std::string encodeError(const std::string& err);
    static std::string encodeInteger(long long val);
    static std::string encodeBulkString(const std::string& str);
    static std::string encodeNullBulk();
};

#endif

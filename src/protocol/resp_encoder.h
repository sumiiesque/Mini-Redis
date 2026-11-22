
#ifndef RESP_ENCODER_H
#define RESP_ENCODER_H

#include <string>
#include "../common/response.h"

class RESPEncoder {
public:
    std::string encode(const Response& response);
};

#endif

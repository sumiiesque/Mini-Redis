#ifndef RESP_PARSER_H
#define RESP_PARSER_H

#include <string>
#include <vector>
#include "../common/command.h"

class RESPParser {
public:
    bool parse(const std::string& request,
               Command& command,
               size_t& consumed);

private:
    bool parseInline(const std::string& request,
                     Command& command,
                     size_t& consumed);
};

#endif

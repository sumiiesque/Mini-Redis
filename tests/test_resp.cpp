#include <iostream>
#include "protocol/resp_parser.h"

int main() {
    std::string request =
        "*3\r\n"
        "$3\r\nSET\r\n"
        "$4\r\nname\r\n"
        "$6\r\nSmriti\r\n";

    Command command;
    size_t consumed = 0;
    RESPParser parser;

    try {
        std::cout << "Request length: " << request.size() << "\n";
        bool ok = parser.parse(request, command, consumed);
        std::cout << "Parsed: " << ok << "\n";
        std::cout << "Command: " << command.name << "\n";
        std::cout << "Arguments: " << command.arg.size() << "\n";
        std::cout << "Consumed: " << consumed << "\n";
    } catch (const std::exception& e) {
        std::cout << "Parser error: " << e.what() << "\n";
    }
}
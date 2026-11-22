#ifndef COMMAND_H
#define COMMAND_H

#include <string>
#include <vector>

using namespace std;

struct Command {
    string name;
    vector<string> arg;
};

#endif
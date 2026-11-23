#ifndef CPU_AFFINITY_H
#define CPU_AFFINITY_H

#include <cstddef>
#include <string>
#include <pthread.h>

class CPUAffinity {
public:
    static bool pinCurrentThread(size_t core_id);
    static bool pinThread(pthread_t thread, size_t core_id);
    static size_t getCoreCount();
    static bool setThreadName(const std::string& name);
};

#endif

#include "cpu_affinity.h"

#include <pthread.h>
#include <thread>
#include <iostream>

#if defined(__linux__)
#include <sched.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_policy.h>
#endif

size_t CPUAffinity::getCoreCount() {
    unsigned int cores = std::thread::hardware_concurrency();
    return cores > 0 ? static_cast<size_t>(cores) : 1;
}

bool CPUAffinity::pinThread(pthread_t thread, size_t core_id) {
    size_t cores = getCoreCount();
    size_t target_core = core_id % cores;

#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(target_core, &cpuset);
    int rc = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    return rc == 0;
#elif defined(__APPLE__)
    thread_affinity_policy_data_t policy = { static_cast<integer_t>(target_core + 1) };
    mach_port_t mach_thread = pthread_mach_thread_np(thread);
    kern_return_t kr = thread_policy_set(
        mach_thread,
        THREAD_AFFINITY_POLICY,
        reinterpret_cast<thread_policy_t>(&policy),
        THREAD_AFFINITY_POLICY_COUNT
    );
    return kr == KERN_SUCCESS;
#else
    (void)thread;
    (void)target_core;
    return false;
#endif
}

bool CPUAffinity::pinCurrentThread(size_t core_id) {
    return pinThread(pthread_self(), core_id);
}

bool CPUAffinity::setThreadName(const std::string& name) {
#if defined(__linux__)
    std::string truncated = name.substr(0, 15);
    return pthread_setname_np(pthread_self(), truncated.c_str()) == 0;
#elif defined(__APPLE__)
    return pthread_setname_np(name.c_str()) == 0;
#else
    (void)name;
    return false;
#endif
}

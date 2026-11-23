#include "thread_pool.h"
#include "cpu_affinity.h"

#include <stdexcept>
#include <iostream>

ThreadPool::ThreadPool(size_t num_threads, bool pin_cores)
    : stopping(false), active_tasks(0), pin_cores(pin_cores) {
    if (num_threads == 0) {
        throw std::invalid_argument("ThreadPool: worker count must be greater than zero");
    }

    size_t available_cores = CPUAffinity::getCoreCount();

    workers.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers.emplace_back([this, i, available_cores]() {
            if (this->pin_cores) {
                CPUAffinity::pinCurrentThread(i % available_cores);
            }
            CPUAffinity::setThreadName("worker-" + std::to_string(i));

            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->condition.wait(lock, [this]() {
                        return this->stopping.load() || !this->tasks.empty();
                    });

                    if (this->stopping.load() && this->tasks.empty()) {
                        return;
                    }

                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                    this->active_tasks++;
                }

                try {
                    task();
                } catch (const std::exception& e) {
                    std::cerr << "ThreadPool: task exception caught: " << e.what() << '\n';
                } catch (...) {
                    std::cerr << "ThreadPool: unknown task exception caught\n";
                }

                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->active_tasks--;
                    if (this->tasks.empty() && this->active_tasks == 0) {
                        this->idle_condition.notify_all();
                    }
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (stopping.load()) {
            throw std::runtime_error("ThreadPool: cannot enqueue task on stopping pool");
        }
        tasks.push(std::move(task));
    }
    condition.notify_one();
}

size_t ThreadPool::pendingTaskCount() {
    std::unique_lock<std::mutex> lock(queue_mutex);
    return tasks.size();
}

void ThreadPool::waitIdle() {
    std::unique_lock<std::mutex> lock(queue_mutex);
    idle_condition.wait(lock, [this]() {
        return this->tasks.empty() && this->active_tasks == 0;
    });
}

void ThreadPool::shutdown() {
    bool expected = false;
    if (stopping.compare_exchange_strong(expected, true)) {
        condition.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
}

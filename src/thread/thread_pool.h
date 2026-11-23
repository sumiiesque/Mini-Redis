#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <atomic>

class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;
    std::condition_variable idle_condition;

    std::atomic<bool> stopping;
    std::atomic<size_t> active_tasks;
    bool pin_cores;

public:
    explicit ThreadPool(size_t num_threads, bool pin_cores = true);
    ~ThreadPool();

    void enqueue(std::function<void()> task);

    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stopping.load()) {
                throw std::runtime_error("ThreadPool: cannot submit task on stopping pool");
            }
            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    size_t workerCount() const noexcept { return workers.size(); }
    size_t pendingTaskCount();
    bool isStopping() const noexcept { return stopping.load(); }
    void waitIdle();
    void shutdown();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
};

#endif

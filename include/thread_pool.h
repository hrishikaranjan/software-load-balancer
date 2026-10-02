// thread_pool.h - Fixed set of worker threads fed by a bounded queue of client sockets.
// Classic producer/consumer: main thread = producer (accept), workers = consumers.
#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
    using Handler = std::function<void(int)>;   // receives the client fd and must close it

    ThreadPool(size_t workers, size_t max_queue, Handler handler);
    ~ThreadPool();

    bool submit(int client_fd);   // false if the queue is full or the pool is stopping
    void shutdown();              // finish queued work, then join all workers (idempotent)

private:
    void worker_loop();

    size_t max_queue_;
    Handler handler_;
    std::vector<std::thread> workers_;
    std::queue<int> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
};

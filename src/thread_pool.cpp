#include "thread_pool.h"

ThreadPool::ThreadPool(size_t workers, size_t max_queue, Handler handler)
    : max_queue_(max_queue), handler_(std::move(handler)) {
    for (size_t i = 0; i < workers; ++i)
        workers_.emplace_back(&ThreadPool::worker_loop, this);
}

ThreadPool::~ThreadPool() { shutdown(); }

bool ThreadPool::submit(int client_fd) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || queue_.size() >= max_queue_) return false;
        queue_.push(client_fd);
    }
    cv_.notify_one();                      // wake exactly one sleeping worker
    return true;
}

void ThreadPool::worker_loop() {
    for (;;) {
        int fd;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // Sleep until there is work or we are told to stop (no busy waiting / CPU burn).
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) return;    // stopping and nothing left to drain
            fd = queue_.front();
            queue_.pop();
        }
        handler_(fd);                      // run outside the lock so workers work in parallel
    }
}

void ThreadPool::shutdown() {
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    cv_.notify_all();
    for (std::thread& t : workers_)
        if (t.joinable()) t.join();
}

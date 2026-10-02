// statistics.h - Lock-free global counters + formatted summary.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include "backend_pool.h"

class Statistics {
public:
    Statistics() : start_(std::chrono::steady_clock::now()) {}

    void request_received()  { total_++; }
    void request_succeeded() { success_++; }
    void request_failed()    { failed_++; }
    void client_connected()  { active_clients_++; }
    void client_disconnected() { active_clients_--; }

    uint64_t total() const { return total_; }
    uint64_t successful() const { return success_; }
    uint64_t failed() const { return failed_; }
    int active_clients() const { return active_clients_; }
    long uptime_seconds() const {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_).count();
    }

    std::string format_summary(const BackendPool& pool) const;

private:
    // std::atomic: increments from many worker threads need no mutex.
    std::atomic<uint64_t> total_{0}, success_{0}, failed_{0};
    std::atomic<int> active_clients_{0};
    std::chrono::steady_clock::time_point start_;
};

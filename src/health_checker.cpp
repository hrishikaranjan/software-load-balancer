#include "health_checker.h"
#include <chrono>
#include <unistd.h>
#include "net_utils.h"

HealthChecker::HealthChecker(BackendPool& pool, int interval_ms, int timeout_ms)
    : pool_(pool), interval_ms_(interval_ms), timeout_ms_(timeout_ms) {}

HealthChecker::~HealthChecker() { stop(); }

bool HealthChecker::probe(const std::string& ip, int port, int timeout_ms) {
    int fd = connect_with_timeout(ip, port, timeout_ms);   // refused / timeout => unreachable
    if (fd < 0) return false;
    bool ok = false;
    if (send_all(fd, "HEALTH\n")) {
        std::string reply;
        if (recv_line(fd, reply, 64, timeout_ms)) ok = (reply.compare(0, 2, "OK") == 0);
    }
    close(fd);                                             // always release the descriptor
    return ok;
}

void HealthChecker::check_all_once() {
    std::vector<Backend> backends = pool_.snapshot();
    for (size_t i = 0; i < backends.size(); ++i) {
        // Network I/O is done WITHOUT holding the pool mutex so slow probes never block routing.
        bool ok = probe(backends[i].ip, backends[i].port, timeout_ms_);
        pool_.report_health(static_cast<int>(i), ok);
    }
}

void HealthChecker::start() { thread_ = std::thread(&HealthChecker::run, this); }

void HealthChecker::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
        lock.unlock();
        check_all_once();
        lock.lock();
        cv_.wait_for(lock, std::chrono::milliseconds(interval_ms_), [this] { return stopping_ || trigger_; });
        trigger_ = false;
    }
}

void HealthChecker::trigger_now() {
    { std::lock_guard<std::mutex> lock(mutex_); trigger_ = true; }
    cv_.notify_all();
}

void HealthChecker::stop() {
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

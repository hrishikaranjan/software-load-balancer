// backend_pool.h - Shared, mutex-protected table of backend servers.
// This is THE shared data structure of the load balancer: worker threads, the health
// checker thread and the main thread all touch it, so every access goes through one mutex.
#pragma once
#include <mutex>
#include <string>
#include <vector>
#include "backend.h"
#include "router.h"

class BackendPool {
public:
    static constexpr int kRecoveryThreshold = 2;   // consecutive good checks to leave RECOVERING

    // Parses "name ip port" lines ('#' = comment). Bad lines are logged and skipped.
    bool load_config(const std::string& path);

    // Picks a backend with the router and increments its active_connections atomically.
    // Returns the index (or -1 if no healthy backend) and copies the backend into 'out'.
    int acquire(Router& router, Backend& out);
    // Counterpart of acquire(): decrements active_connections and updates counters.
    void release(int index, bool success);

    // Health-check result from HealthChecker (drives the state machine).
    void report_health(int index, bool reachable);
    // Passive detection: forwarding to this backend failed -> mark it UNHEALTHY at once.
    void report_forward_failure(int index);

    std::vector<Backend> snapshot() const;          // consistent copy for reading/printing
    size_t size() const;
    void count_states(int& healthy, int& unhealthy, int& recovering) const;

private:
    void mark_unhealthy_locked(Backend& b, bool first_check);

    mutable std::mutex mutex_;
    std::vector<Backend> backends_;
};

// health_checker.h - Background thread that probes every backend periodically.
#pragma once
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include "backend_pool.h"

class HealthChecker {
public:
    HealthChecker(BackendPool& pool, int interval_ms, int timeout_ms);
    ~HealthChecker();                 // calls stop()

    void check_all_once();            // synchronous round (used once at start-up)
    void start();                     // spawn the checker thread
    void stop();                      // wake it up and join (graceful shutdown)
    void trigger_now();               // ask the health thread to run a round immediately (GUI button)

    // TCP connect + application-level "HEALTH" / "OK" handshake.
    static bool probe(const std::string& ip, int port, int timeout_ms);

private:
    void run();

    BackendPool& pool_;
    int interval_ms_;
    int timeout_ms_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;      // lets stop() interrupt the sleep between rounds
    bool stopping_ = false;
    bool trigger_ = false;            // set by trigger_now(), consumed by run()
};

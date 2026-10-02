// backend.h - Data model describing one backend server as seen by the load balancer.
#pragma once
#include <cstdint>
#include <ctime>
#include <string>

enum class BackendStatus { HEALTHY, UNHEALTHY, RECOVERING };

inline const char* status_to_string(BackendStatus s) {
    switch (s) {
        case BackendStatus::HEALTHY:    return "HEALTHY";
        case BackendStatus::UNHEALTHY:  return "UNHEALTHY";
        case BackendStatus::RECOVERING: return "RECOVERING";
    }
    return "UNKNOWN";
}

struct Backend {
    int id = 0;                         // 1-based server number (Server 1, Server 2, ...)
    std::string name;                   // name from servers.conf
    std::string ip;                     // IPv4 address
    int port = 0;                       // TCP port
    BackendStatus status = BackendStatus::UNHEALTHY;  // unknown until first health check
    int active_connections = 0;         // requests currently being forwarded
    uint64_t total_requests = 0;        // forwarding attempts finished
    uint64_t successful_requests = 0;
    uint64_t failed_requests = 0;
    time_t last_health_check = 0;       // 0 = never checked
    int consecutive_successes = 0;      // used by the RECOVERING state
    bool ever_healthy = false;          // distinguishes "first start" from "recovery"
};

// admin_client.h - Client side of the admin channel (used by lb_gui, lbctl and the tests).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct BackendInfo {
    int id = 0;
    std::string name, ip, status;
    int port = 0;
    int active = 0;
    uint64_t total = 0, ok = 0, failed = 0;
    long last_check = 0;               // epoch seconds, 0 = never
};

struct LbStatus {
    int pid = 0, port = 0, workers = 0;
    long uptime = 0;
    std::string algorithm;
    uint64_t total = 0, success = 0, failed = 0;
    int active_clients = 0, healthy = 0, unhealthy = 0, recovering = 0;
    std::vector<BackendInfo> backends;
};

const char* const kDefaultAdminSocket = "run/lb_admin.sock";

// Sends one command and reads the whole reply (until the server closes). false if the
// balancer is not running or does not answer.
bool admin_request(const std::string& socket_path, const std::string& command,
                   std::string& reply, int timeout_ms = 500);
bool parse_status(const std::string& reply, LbStatus& out);

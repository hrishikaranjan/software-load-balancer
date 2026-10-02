#include "backend_pool.h"
#include <fstream>
#include <sstream>
#include "logger.h"

bool BackendPool::load_config(const std::string& path) {
    std::ifstream in(path);
    if (!in) { LOG_ERROR("Cannot open config file '%s'", path.c_str()); return false; }

    std::lock_guard<std::mutex> lock(mutex_);
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream iss(line);
        std::string name, ip, extra;
        int port = 0;
        if (!(iss >> name)) continue;                       // blank line
        if (!(iss >> ip >> port) || (iss >> extra) || port < 1 || port > 65535) {
            LOG_WARN("Config %s:%d is invalid, skipped: '%s'", path.c_str(), line_no, line.c_str());
            continue;
        }
        Backend b;
        b.id = static_cast<int>(backends_.size()) + 1;
        b.name = name; b.ip = ip; b.port = port;
        backends_.push_back(b);
        LOG_INFO("Loaded backend Server %d: %s (%s:%d)", b.id, name.c_str(), ip.c_str(), port);
    }
    return !backends_.empty();
}

int BackendPool::acquire(Router& router, Backend& out) {
    std::lock_guard<std::mutex> lock(mutex_);              // RAII: unlocks on every return path
    int idx = router.select(backends_);
    if (idx < 0) return -1;
    backends_[idx].active_connections++;
    out = backends_[idx];
    return idx;
}

void BackendPool::release(int index, bool success) {
    std::lock_guard<std::mutex> lock(mutex_);
    Backend& b = backends_[index];
    if (b.active_connections > 0) b.active_connections--;
    b.total_requests++;
    if (success) b.successful_requests++; else b.failed_requests++;
}

void BackendPool::mark_unhealthy_locked(Backend& b, bool first_check) {
    if (b.status != BackendStatus::UNHEALTHY || first_check)
        LOG_WARN("Backend Server %d (%s:%d) is UNHEALTHY", b.id, b.ip.c_str(), b.port);
    b.status = BackendStatus::UNHEALTHY;
    b.consecutive_successes = 0;
}

void BackendPool::report_health(int index, bool reachable) {
    std::lock_guard<std::mutex> lock(mutex_);
    Backend& b = backends_[index];
    bool first_check = (b.last_health_check == 0);
    b.last_health_check = std::time(nullptr);

    if (!reachable) { mark_unhealthy_locked(b, first_check); return; }

    // State machine:  UNHEALTHY -> RECOVERING -> HEALTHY  (first start: UNHEALTHY -> HEALTHY)
    switch (b.status) {
        case BackendStatus::UNHEALTHY:
            if (!b.ever_healthy) {
                b.status = BackendStatus::HEALTHY;
                b.ever_healthy = true;
                LOG_INFO("Backend Server %d is healthy (%s:%d)", b.id, b.ip.c_str(), b.port);
            } else {
                b.status = BackendStatus::RECOVERING;
                b.consecutive_successes = 1;
                LOG_INFO("Backend Server %d is reachable again - RECOVERING (%d/%d)",
                         b.id, b.consecutive_successes, kRecoveryThreshold);
            }
            break;
        case BackendStatus::RECOVERING:
            if (++b.consecutive_successes >= kRecoveryThreshold) {
                b.status = BackendStatus::HEALTHY;
                LOG_INFO("Backend Server %d recovered and is HEALTHY again", b.id);
            }
            break;
        case BackendStatus::HEALTHY:
            break;
    }
}

void BackendPool::report_forward_failure(int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    mark_unhealthy_locked(backends_[index], false);
}

std::vector<Backend> BackendPool::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return backends_;
}

size_t BackendPool::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return backends_.size();
}

void BackendPool::count_states(int& healthy, int& unhealthy, int& recovering) const {
    std::lock_guard<std::mutex> lock(mutex_);
    healthy = unhealthy = recovering = 0;
    for (const Backend& b : backends_) {
        if (b.status == BackendStatus::HEALTHY) healthy++;
        else if (b.status == BackendStatus::RECOVERING) recovering++;
        else unhealthy++;
    }
}

#include "statistics.h"
#include <sstream>

std::string Statistics::format_summary(const BackendPool& pool) const {
    std::vector<Backend> backends = pool.snapshot();
    int healthy, unhealthy, recovering;
    pool.count_states(healthy, unhealthy, recovering);
    long uptime = std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::steady_clock::now() - start_).count();

    std::ostringstream o;
    o << "\n========== LOAD BALANCER STATISTICS ==========\n\n";
    o << "Total Requests       : " << total_.load() << "\n";
    o << "Successful Requests  : " << success_.load() << "\n";
    o << "Failed Requests      : " << failed_.load() << "\n";
    o << "Active Connections   : " << active_clients_.load() << "\n\n";
    for (const Backend& b : backends) {
        o << "Server " << b.id << " Requests    : " << b.total_requests
          << "   (ok " << b.successful_requests << ", failed " << b.failed_requests
          << ", " << status_to_string(b.status) << ", " << b.ip << ":" << b.port << ")\n";
    }
    o << "\nHealthy Backends     : " << healthy << "\n";
    o << "Unhealthy Backends   : " << unhealthy << "\n";
    o << "Recovering Backends  : " << recovering << "\n";
    o << "Uptime (seconds)     : " << uptime << "\n";
    o << "\n==============================================\n";
    return o.str();
}

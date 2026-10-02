#include "router.h"

int RoundRobinRouter::select(const std::vector<Backend>& backends) {
    const size_t n = backends.size();
    for (size_t i = 0; i < n; ++i) {
        size_t idx = (next_ + i) % n;
        if (backends[idx].status == BackendStatus::HEALTHY) {
            next_ = (idx + 1) % n;      // next request starts after the chosen server
            return static_cast<int>(idx);
        }
    }
    return -1;                          // nobody is healthy
}

int LeastConnectionsRouter::select(const std::vector<Backend>& backends) {
    const size_t n = backends.size();
    int best = -1;
    for (size_t i = 0; i < n; ++i) {
        size_t idx = (next_ + i) % n;
        if (backends[idx].status != BackendStatus::HEALTHY) continue;
        if (best < 0 || backends[idx].active_connections < backends[best].active_connections)
            best = static_cast<int>(idx);
    }
    if (best >= 0) next_ = (static_cast<size_t>(best) + 1) % n;
    return best;
}

std::unique_ptr<Router> make_router(const std::string& algorithm) {
    if (algorithm == "rr" || algorithm == "round-robin")
        return std::unique_ptr<Router>(new RoundRobinRouter());
    if (algorithm == "lc" || algorithm == "least-connections")
        return std::unique_ptr<Router>(new LeastConnectionsRouter());
    return nullptr;
}

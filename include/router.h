// router.h - Load-balancing strategies (Strategy pattern).
// To add a new algorithm: derive from Router, implement select(), register in make_router().
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "backend.h"

class Router {
public:
    virtual ~Router() = default;
    // Returns the index of the chosen backend or -1 if none is HEALTHY.
    // NOTE: always called by BackendPool while its mutex is held, so
    // implementations do not need their own locking.
    virtual int select(const std::vector<Backend>& backends) = 0;
    virtual const char* name() const = 0;
};

// Cycles through the servers in order and skips every non-HEALTHY one.
class RoundRobinRouter : public Router {
public:
    int select(const std::vector<Backend>& backends) override;
    const char* name() const override { return "round-robin"; }
private:
    size_t next_ = 0;   // index where the next search starts
};

// Chooses the healthy server with the fewest active connections.
class LeastConnectionsRouter : public Router {
public:
    int select(const std::vector<Backend>& backends) override;
    const char* name() const override { return "least-connections"; }
private:
    size_t next_ = 0;   // rotating start so ties are shared fairly
};

// "rr" / "round-robin" or "lc" / "least-connections". Returns nullptr if unknown.
std::unique_ptr<Router> make_router(const std::string& algorithm);

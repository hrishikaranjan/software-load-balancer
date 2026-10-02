// Unit test for the routing algorithms (no sockets needed).
#include <cassert>
#include <cstdio>
#include "router.h"

static std::vector<Backend> make(BackendStatus a, BackendStatus b, BackendStatus c) {
    std::vector<Backend> v(3);
    BackendStatus s[3] = {a, b, c};
    for (int i = 0; i < 3; ++i) { v[i].id = i + 1; v[i].status = s[i]; }
    return v;
}

int main() {
    using S = BackendStatus;
    {   // all healthy: 0,1,2,0,1,2
        RoundRobinRouter r; auto v = make(S::HEALTHY, S::HEALTHY, S::HEALTHY);
        int expect[6] = {0, 1, 2, 0, 1, 2};
        for (int e : expect) assert(r.select(v) == e);
    }
    {   // server 2 down: 0,2,0,2
        RoundRobinRouter r; auto v = make(S::HEALTHY, S::UNHEALTHY, S::HEALTHY);
        int expect[4] = {0, 2, 0, 2};
        for (int e : expect) assert(r.select(v) == e);
    }
    {   // RECOVERING servers receive no traffic
        RoundRobinRouter r; auto v = make(S::HEALTHY, S::RECOVERING, S::HEALTHY);
        for (int i = 0; i < 6; ++i) assert(r.select(v) != 1);
    }
    {   // nobody healthy
        RoundRobinRouter r; auto v = make(S::UNHEALTHY, S::UNHEALTHY, S::RECOVERING);
        assert(r.select(v) == -1);
    }
    {   // least connections picks the least loaded healthy server
        LeastConnectionsRouter r; auto v = make(S::HEALTHY, S::HEALTHY, S::HEALTHY);
        v[0].active_connections = 5; v[1].active_connections = 1; v[2].active_connections = 3;
        assert(r.select(v) == 1);
        v[1].status = S::UNHEALTHY;
        assert(r.select(v) == 2);
    }
    assert(make_router("rr") && make_router("lc") && !make_router("nope"));
    std::printf("test_router: all assertions passed\n");
    return 0;
}

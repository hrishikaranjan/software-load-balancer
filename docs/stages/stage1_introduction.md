# Stage 1 - Introduction

**Project title:** Software-Defined Load Balancer & Health-Check Router (Linux, C/C++)

**Problem statement.** A service running on one server fails when that server fails and slows down when too many clients connect. Hardware load balancers are expensive and opaque.
The problem is to build a software load balancer on Linux that distributes client requests over several backend servers, automatically detects failed servers, stops routing to them, and starts routing again after recovery.

**Motivation.** Load balancers (HAProxy, NGINX, Envoy, LVS) are core infrastructure of every large service. Building one from system calls exposes how sockets, threads, signals and the kernel really work.

**Objectives**
1. Implement TCP backend servers, a load balancer and a client in C++ on Linux.
2. Implement Round Robin routing (extensible to Least Connections) that skips unhealthy servers.
3. Implement periodic health checks with HEALTHY / UNHEALTHY / RECOVERING states.
4. Handle concurrent clients with threads and correct synchronisation.
5. Graceful shutdown with signals; logging; statistics.
6. Demonstrate failure and recovery; test and document everything.

**Scope.** In: TCP text protocol on localhost/LAN, 1 balancer + N backends, RR + LC, health checks, logging, statistics, tests. Out: HTTP parsing, TLS, GUI/web UI, distributed balancers, persistent storage.

**Real-world relevance.** Same concepts as HAProxy/NGINX upstreams, Kubernetes service routing, cloud L4 load balancers: health checks, failover, connection draining.

**Technologies.** C++17, Linux syscalls (sockets, poll, fcntl, sigaction), `std::thread/mutex/condition_variable/atomic`, GNU make, g++, Bash (build/test only), Git/GitHub, Linux tools (ps, top, ss, lsof, kill, strace).

**Expected outcome.** Three commands (`make`, run servers + balancer, run client) show requests rotating across servers; killing one server is detected within about one health interval, traffic continues on the others, restarting it brings it back, Ctrl+C prints final statistics.

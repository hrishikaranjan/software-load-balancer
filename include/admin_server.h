// admin_server.h - Local control/monitoring channel of the load balancer (UNIX domain socket).
//
// The GUI (lb_gui) and the command-line tool (lbctl) talk to the REAL running balancer through
// this channel. Text protocol, one command per connection:
//   PING   -> "PONG"
//   STATUS -> key=value lines + one "backend=..." line per server, ending with "END"
//   CHECK  -> asks the health thread to probe all backends immediately -> "OK ..."
#pragma once
#include <atomic>
#include <string>
#include <thread>
#include "backend_pool.h"
#include "health_checker.h"
#include "statistics.h"

class AdminServer {
public:
    AdminServer(const std::string& socket_path, BackendPool& pool, Statistics& stats,
                HealthChecker& checker, const std::string& algorithm, int port, size_t workers);
    ~AdminServer();

    bool start();     // create the socket and spawn the admin thread
    void stop();      // stop the thread, close and unlink the socket (idempotent)

private:
    void run();
    void handle(int fd);
    std::string build_status() const;

    std::string path_;
    BackendPool& pool_;
    Statistics& stats_;
    HealthChecker& checker_;
    std::string algorithm_;
    int port_;
    size_t workers_;
    int listen_fd_ = -1;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};

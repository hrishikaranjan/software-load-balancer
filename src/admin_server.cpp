#include "admin_server.h"
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "logger.h"
#include "net_utils.h"

AdminServer::AdminServer(const std::string& socket_path, BackendPool& pool, Statistics& stats,
                         HealthChecker& checker, const std::string& algorithm, int port, size_t workers)
    : path_(socket_path), pool_(pool), stats_(stats), checker_(checker),
      algorithm_(algorithm), port_(port), workers_(workers) {}

AdminServer::~AdminServer() { stop(); }

bool AdminServer::start() {
    // make the parent directory (e.g. run/) if needed - mkdir(2) system call
    size_t slash = path_.rfind('/');
    if (slash != std::string::npos && slash > 0) mkdir(path_.substr(0, slash).c_str(), 0755);

    listen_fd_ = create_unix_listen_socket(path_, 8);
    if (listen_fd_ < 0) {
        LOG_WARN("Admin interface disabled: cannot create %s: %s", path_.c_str(), std::strerror(errno));
        return false;
    }
    thread_ = std::thread(&AdminServer::run, this);
    LOG_INFO("Admin interface listening on %s (used by lb_gui / lbctl)", path_.c_str());
    return true;
}

void AdminServer::stop() {
    stopping_ = true;
    if (thread_.joinable()) thread_.join();
    if (listen_fd_ >= 0) { close(listen_fd_); listen_fd_ = -1; unlink(path_.c_str()); }
}

void AdminServer::run() {
    while (!stopping_) {
        pollfd p = {listen_fd_, POLLIN, 0};
        int pr = poll(&p, 1, 300);                    // timeout: re-check stopping_ regularly
        if (pr <= 0) continue;
        int fd = accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) continue;
        handle(fd);                                   // commands are tiny: handle inline
        close(fd);
    }
}

void AdminServer::handle(int fd) {
    std::string cmd;
    if (!recv_line(fd, cmd, 128, 500)) return;
    while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) cmd.pop_back();

    if (cmd == "STATUS") {
        send_all(fd, build_status());                 // polled every second: not logged
    } else if (cmd == "PING") {
        send_all(fd, "PONG\n");
    } else if (cmd == "CHECK") {
        LOG_INFO("Admin request: immediate health check of all backends");
        checker_.trigger_now();
        send_all(fd, "OK health check started\n");
    } else {
        send_all(fd, "ERR unknown command\n");
    }
}

std::string AdminServer::build_status() const {
    int healthy, unhealthy, recovering;
    pool_.count_states(healthy, unhealthy, recovering);
    std::ostringstream o;
    o << "OK\n";
    o << "pid=" << getpid() << "\n";
    o << "port=" << port_ << "\n";
    o << "algorithm=" << algorithm_ << "\n";
    o << "workers=" << workers_ << "\n";
    o << "uptime=" << stats_.uptime_seconds() << "\n";
    o << "total=" << stats_.total() << "\n";
    o << "success=" << stats_.successful() << "\n";
    o << "failed=" << stats_.failed() << "\n";
    o << "active_clients=" << stats_.active_clients() << "\n";
    o << "healthy=" << healthy << "\n";
    o << "unhealthy=" << unhealthy << "\n";
    o << "recovering=" << recovering << "\n";
    for (const Backend& b : pool_.snapshot()) {       // consistent copy taken under the pool mutex
        o << "backend=" << b.id << "|" << b.name << "|" << b.ip << "|" << b.port << "|"
          << status_to_string(b.status) << "|" << b.active_connections << "|" << b.total_requests << "|"
          << b.successful_requests << "|" << b.failed_requests << "|" << static_cast<long>(b.last_health_check) << "\n";
    }
    o << "END\n";
    return o.str();
}

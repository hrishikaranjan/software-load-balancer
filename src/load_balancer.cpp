// load_balancer.cpp - Main program: accept loop + wiring of all modules.
//
// Threads in this process:
//   main thread     : poll()/accept() loop, handles signals' flags
//   health thread   : probes backends every N ms
//   worker threads  : take a client fd from the queue, forward the request, reply
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <string>
#include <unistd.h>
#include <getopt.h>
#include <sys/socket.h>

#include "admin_server.h"
#include "backend_pool.h"
#include "health_checker.h"
#include "logger.h"
#include "net_utils.h"
#include "router.h"
#include "signal_handler.h"
#include "statistics.h"
#include "thread_pool.h"

namespace {

const int kBackendConnectTimeoutMs = 1000;
const int kIoTimeoutMs = 5000;

struct Options {
    int port = 8080;
    std::string config = "config/servers.conf";
    std::string algorithm = "rr";
    std::string log_file = "logs/load_balancer.log";
    std::string admin_socket = "run/lb_admin.sock";   // control channel for lb_gui / lbctl
    int health_interval_ms = 2000;
    size_t workers = 8;
};

void usage(const char* prog) {
    std::printf(
        "Usage: %s [options]\n"
        "  -p PORT    listening port            (default 8080)\n"
        "  -c FILE    backend config file       (default config/servers.conf)\n"
        "  -a ALGO    rr | lc                   (default rr)\n"
        "  -i MS      health-check interval ms  (default 2000)\n"
        "  -w N       worker threads            (default 8)\n"
        "  -l FILE    log file                  (default logs/load_balancer.log)\n"
        "  -s PATH    admin UNIX socket for GUI (default run/lb_admin.sock)\n"
        "  -h         help\n", prog);
}

bool parse_args(int argc, char** argv, Options& o) {
    int c;
    while ((c = getopt(argc, argv, "p:c:a:i:w:l:s:h")) != -1) {
        switch (c) {
            case 'p': o.port = std::atoi(optarg); break;
            case 'c': o.config = optarg; break;
            case 'a': o.algorithm = optarg; break;
            case 'i': o.health_interval_ms = std::atoi(optarg); break;
            case 'w': o.workers = static_cast<size_t>(std::atoi(optarg)); break;
            case 'l': o.log_file = optarg; break;
            case 's': o.admin_socket = optarg; break;
            default:  usage(argv[0]); return false;
        }
    }
    if (o.port < 1 || o.port > 65535 || o.health_interval_ms < 50 || o.workers < 1) {
        std::fprintf(stderr, "Invalid option value\n");
        return false;
    }
    return true;
}

void strip_newline(std::string& s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
}

// Forwards one request to a healthy backend. Retries on another backend if one fails,
// so a crash between two health checks is invisible to the client.
bool forward_request(BackendPool& pool, Router& router, const std::string& request,
                     std::string& response) {
    const size_t attempts = pool.size();
    for (size_t attempt = 0; attempt < attempts; ++attempt) {
        Backend b;
        int idx = pool.acquire(router, b);       // chooses backend + active_connections++
        if (idx < 0) break;                      // no HEALTHY backend

        int bfd = connect_with_timeout(b.ip, b.port, kBackendConnectTimeoutMs);
        if (bfd < 0) {
            int err = errno;
            LOG_ERROR("Cannot connect to Backend Server %d (%s:%d): %s",
                      b.id, b.ip.c_str(), b.port, std::strerror(err));
            pool.release(idx, false);
            pool.report_forward_failure(idx);    // passive health detection
            continue;                            // try the next healthy server
        }

        LOG_INFO("Request routed to Server %d (%s:%d)", b.id, b.ip.c_str(), b.port);
        bool ok = send_all(bfd, request + "\n") && recv_line(bfd, response, 8192, kIoTimeoutMs);
        close(bfd);
        pool.release(idx, ok);
        if (ok) return true;

        LOG_WARN("Backend Server %d failed while handling the request", b.id);
        pool.report_forward_failure(idx);
    }
    return false;
}

// Runs inside a worker thread. Owns and closes client_fd.
void handle_client(int client_fd, BackendPool& pool, Router& router, Statistics& stats) {
    std::string request;
    if (!recv_line(client_fd, request, 4096, kIoTimeoutMs)) {
        LOG_WARN("Client (fd=%d) sent no request, closing", client_fd);
        close(client_fd);
        stats.client_disconnected();
        return;
    }
    strip_newline(request);
    stats.request_received();

    std::string response;
    if (forward_request(pool, router, request, response)) {
        stats.request_succeeded();      // count first: when the client sees the reply, statistics are already up to date
        send_all(client_fd, response);
    } else {
        LOG_ERROR("Request failed: no healthy backend could serve '%s'", request.c_str());
        stats.request_failed();
        send_all(client_fd, "ERROR: no healthy backend available\n");
    }
    close(client_fd);
    stats.client_disconnected();
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) return 1;

    install_signal_handlers();
    if (!logger_init(opt.log_file, true))
        std::fprintf(stderr, "Warning: cannot open log file %s\n", opt.log_file.c_str());

    std::unique_ptr<Router> router = make_router(opt.algorithm);
    if (!router) { LOG_ERROR("Unknown algorithm '%s' (use rr or lc)", opt.algorithm.c_str()); return 1; }

    LOG_INFO("Load balancer started (pid=%d, port=%d, algorithm=%s, workers=%zu)",
             getpid(), opt.port, router->name(), opt.workers);

    BackendPool pool;
    if (!pool.load_config(opt.config)) {
        LOG_ERROR("No valid backend servers configured in '%s'", opt.config.c_str());
        logger_close();
        return 1;
    }

    Statistics stats;
    HealthChecker checker(pool, opt.health_interval_ms, 1000);
    checker.check_all_once();       // know the state of every backend before accepting clients
    checker.start();

    ThreadPool workers(opt.workers, 1024, [&](int fd) { handle_client(fd, pool, *router, stats); });

    int listen_fd = create_listen_socket(opt.port, SOMAXCONN);
    if (listen_fd < 0) {
        LOG_ERROR("Cannot listen on port %d: %s", opt.port, std::strerror(errno));
        workers.shutdown();
        checker.stop();
        logger_close();
        return 1;
    }
    LOG_INFO("Listening for clients on port %d", opt.port);

    // Local admin channel (read-only status + "check now"); the core logic does not depend on it.
    AdminServer admin(opt.admin_socket, pool, stats, checker, router->name(), opt.port, opt.workers);
    admin.start();

    while (!shutdown_requested()) {
        if (consume_usr1()) log_raw(stats.format_summary(pool));   // kill -USR1 <pid> = live stats

        // poll() with a timeout: the loop re-checks the shutdown flag at least every 500 ms
        // even if the signal was delivered to another thread.
        pollfd p = {listen_fd, POLLIN, 0};
        int pr = poll(&p, 1, 500);
        if (pr < 0) { if (errno == EINTR) continue; LOG_ERROR("poll: %s", std::strerror(errno)); break; }
        if (pr == 0) continue;

        sockaddr_in caddr;
        socklen_t clen = sizeof caddr;
        int cfd = accept(listen_fd, reinterpret_cast<sockaddr*>(&caddr), &clen);
        if (cfd < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == ECONNABORTED) continue;
            LOG_ERROR("accept: %s", std::strerror(errno));
            continue;
        }
        char ipbuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &caddr.sin_addr, ipbuf, sizeof ipbuf);
        LOG_INFO("Client connected: %s:%d (fd=%d)", ipbuf, ntohs(caddr.sin_port), cfd);

        stats.client_connected();
        if (!workers.submit(cfd)) {
            LOG_WARN("Server busy, rejecting client fd=%d", cfd);
            send_all(cfd, "ERROR: server busy\n");
            close(cfd);
            stats.client_disconnected();
        }
    }

    LOG_INFO("Load balancer shutting down (signal %d)", last_shutdown_signal());
    close(listen_fd);               // 1. stop accepting new connections
    workers.shutdown();             // 2. let in-flight requests finish, join workers
    admin.stop();                   // 3. close + unlink the admin socket
    checker.stop();                 // 4. stop the health thread
    log_raw(stats.format_summary(pool));   // 5. final statistics (console + log file)
    LOG_INFO("Shutdown complete");
    logger_close();                 // 6. flush and close the log file
    return 0;
}

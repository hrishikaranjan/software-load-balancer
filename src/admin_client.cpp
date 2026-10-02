#include "admin_client.h"
#include <cerrno>
#include <cstdlib>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>
#include "net_utils.h"

bool admin_request(const std::string& socket_path, const std::string& command,
                   std::string& reply, int timeout_ms) {
    int fd = connect_unix(socket_path);               // fails at once if the balancer is not running
    if (fd < 0) return false;
    bool ok = send_all(fd, command + "\n");
    reply.clear();
    char buf[2048];
    while (ok) {
        pollfd p = {fd, POLLIN, 0};
        int pr = poll(&p, 1, timeout_ms);
        if (pr < 0 && errno == EINTR) continue;
        if (pr <= 0) { ok = !reply.empty(); break; }
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;                            // EOF: server finished the reply
        reply.append(buf, static_cast<size_t>(n));
    }
    close(fd);
    return ok && !reply.empty();
}

bool parse_status(const std::string& reply, LbStatus& out) {
    std::istringstream in(reply);
    std::string line;
    if (!std::getline(in, line) || line != "OK") return false;
    out = LbStatus();
    bool complete = false;
    while (std::getline(in, line)) {
        if (line == "END") { complete = true; break; }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "pid") out.pid = std::atoi(v.c_str());
        else if (k == "port") out.port = std::atoi(v.c_str());
        else if (k == "algorithm") out.algorithm = v;
        else if (k == "workers") out.workers = std::atoi(v.c_str());
        else if (k == "uptime") out.uptime = std::atol(v.c_str());
        else if (k == "total") out.total = std::strtoull(v.c_str(), nullptr, 10);
        else if (k == "success") out.success = std::strtoull(v.c_str(), nullptr, 10);
        else if (k == "failed") out.failed = std::strtoull(v.c_str(), nullptr, 10);
        else if (k == "active_clients") out.active_clients = std::atoi(v.c_str());
        else if (k == "healthy") out.healthy = std::atoi(v.c_str());
        else if (k == "unhealthy") out.unhealthy = std::atoi(v.c_str());
        else if (k == "recovering") out.recovering = std::atoi(v.c_str());
        else if (k == "backend") {
            std::vector<std::string> f;
            std::istringstream fs(v);
            std::string part;
            while (std::getline(fs, part, '|')) f.push_back(part);
            if (f.size() < 10) continue;
            BackendInfo b;
            b.id = std::atoi(f[0].c_str()); b.name = f[1]; b.ip = f[2]; b.port = std::atoi(f[3].c_str());
            b.status = f[4]; b.active = std::atoi(f[5].c_str());
            b.total = std::strtoull(f[6].c_str(), nullptr, 10);
            b.ok = std::strtoull(f[7].c_str(), nullptr, 10);
            b.failed = std::strtoull(f[8].c_str(), nullptr, 10);
            b.last_check = std::atol(f[9].c_str());
            out.backends.push_back(b);
        }
    }
    return complete;
}

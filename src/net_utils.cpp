#include "net_utils.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
// close() may overwrite errno; callers want the original error.
void close_keep_errno(int fd) { int e = errno; close(fd); errno = e; }
}

int create_listen_socket(int port, int backlog) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);          // TCP/IPv4 endpoint = a file descriptor
    if (fd < 0) return -1;
    int opt = 1;
    // SO_REUSEADDR lets us restart immediately without "Address already in use" (TIME_WAIT).
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);          // htonl/htons: host -> network byte order
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 || listen(fd, backlog) < 0) {
        close_keep_errno(fd);
        return -1;
    }
    return fd;
}

int connect_with_timeout(const std::string& ip, int port, int timeout_ms) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) { errno = EINVAL; close_keep_errno(fd); return -1; }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);            // connect() returns immediately
    int rc = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    if (rc < 0) {
        if (errno != EINPROGRESS) { close_keep_errno(fd); return -1; }   // e.g. ECONNREFUSED
        pollfd p = {fd, POLLOUT, 0};
        int pr;
        do { pr = poll(&p, 1, timeout_ms); } while (pr < 0 && errno == EINTR);
        if (pr == 0) { errno = ETIMEDOUT; close_keep_errno(fd); return -1; }
        if (pr < 0)  { close_keep_errno(fd); return -1; }
        int err = 0;
        socklen_t len = sizeof err;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);   // result of the async connect
        if (err != 0) { errno = err; close_keep_errno(fd); return -1; }
    }
    fcntl(fd, F_SETFL, flags);                         // back to blocking mode
    return fd;
}

bool send_all(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        // MSG_NOSIGNAL: a closed peer gives EPIPE instead of killing us with SIGPIPE.
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0) { if (errno == EINTR) continue; return false; }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool recv_line(int fd, std::string& out, size_t max_len, int timeout_ms) {
    out.clear();
    char buf[1024];
    while (out.size() < max_len && out.find('\n') == std::string::npos) {
        pollfd p = {fd, POLLIN, 0};
        int pr = poll(&p, 1, timeout_ms);
        if (pr < 0 && errno == EINTR) continue;
        if (pr <= 0) break;                             // timeout or error
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;                              // 0 = peer closed (EOF)
        out.append(buf, static_cast<size_t>(n));
    }
    return !out.empty();
}

int create_unix_listen_socket(const std::string& path, int backlog) {
    sockaddr_un addr;
    if (path.size() >= sizeof addr.sun_path) { errno = ENAMETOOLONG; return -1; }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);          // local IPC endpoint, also a file descriptor
    if (fd < 0) return -1;
    std::memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size());
    unlink(path.c_str());                              // remove a stale socket file from a crashed run
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) { close_keep_errno(fd); return -1; }
    chmod(path.c_str(), 0600);                         // only the owner may talk to the admin channel
    if (listen(fd, backlog) < 0) { close_keep_errno(fd); return -1; }
    return fd;
}

int connect_unix(const std::string& path) {
    sockaddr_un addr;
    if (path.size() >= sizeof addr.sun_path) { errno = ENAMETOOLONG; return -1; }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    std::memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size());
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) { close_keep_errno(fd); return -1; }
    return fd;
}

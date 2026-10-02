// net_utils.h - Thin, well-commented wrappers around the BSD socket system calls.
#pragma once
#include <cstddef>
#include <string>

// socket() + SO_REUSEADDR + bind() + listen(). Returns fd or -1 (errno set).
int create_listen_socket(int port, int backlog);

// Non-blocking connect() + poll() so an unreachable host cannot block a thread forever.
// Returns a connected, blocking fd or -1 (errno set).
int connect_with_timeout(const std::string& ip, int port, int timeout_ms);

// send() until everything is written (handles partial writes and EINTR).
bool send_all(int fd, const std::string& data);

// recv() until '\n', EOF, max_len bytes or timeout. Returns true if any data arrived.
bool recv_line(int fd, std::string& out, size_t max_len, int timeout_ms);

// ---- UNIX domain sockets (used for the local admin channel between GUI/lbctl and the balancer) ----
// socket(AF_UNIX) + unlink stale file + bind + chmod 0600 (owner only) + listen. fd or -1.
int create_unix_listen_socket(const std::string& path, int backlog);
// connect() to a UNIX socket path. fd or -1 (errno set, e.g. ENOENT/ECONNREFUSED when not running).
int connect_unix(const std::string& path);

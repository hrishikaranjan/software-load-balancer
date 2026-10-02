// client.cpp - Test client.
//   ./client                      interactive: asks for a request, sends it once
//   ./client -m Hello -n 9        send 9 requests (shows round robin distribution)
//   ./client -m Hi -n 5 -c 10     10 concurrent client threads x 5 requests
#include <cstdio>
#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "net_utils.h"

namespace {

std::mutex g_out_mutex;
std::map<int, int> g_per_server;     // server id -> responses
int g_ok = 0, g_failed = 0;

bool send_one(const std::string& host, int port, const std::string& msg, std::string& response) {
    int fd = connect_with_timeout(host, port, 3000);
    if (fd < 0) return false;
    bool ok = send_all(fd, msg + "\n") && recv_line(fd, response, 8192, 8000);
    close(fd);
    return ok;
}

int parse_server_id(const std::string& response) {
    size_t pos = response.find("Backend Server ");
    return (pos == std::string::npos) ? -1 : std::atoi(response.c_str() + pos + 15);
}

void run_requests(const std::string& host, int port, const std::string& msg, int count,
                  int delay_ms, bool quiet, int thread_no) {
    for (int i = 1; i <= count; ++i) {
        std::string text = (count > 1 || thread_no > 0) ? msg + " #" + std::to_string(i) : msg;
        std::string response;
        bool ok = send_one(host, port, text, response) && response.compare(0, 5, "ERROR") != 0;
        while (!response.empty() && (response.back() == '\n' || response.back() == '\r')) response.pop_back();
        {
            std::lock_guard<std::mutex> lock(g_out_mutex);
            if (ok) { g_ok++; int id = parse_server_id(response); if (id > 0) g_per_server[id]++; }
            else g_failed++;
            if (!quiet) {
                if (ok) std::printf("[%d.%d] %s\n", thread_no, i, response.c_str());
                else    std::printf("[%d.%d] FAILED %s\n", thread_no, i, response.c_str());
            }
        }
        if (delay_ms > 0) usleep(static_cast<useconds_t>(delay_ms) * 1000);
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1", message;
    int port = 8080, count = 1, concurrency = 1, delay_ms = 0;
    bool quiet = false;
    int c;
    while ((c = getopt(argc, argv, "H:p:n:m:c:d:qh")) != -1) {
        switch (c) {
            case 'H': host = optarg; break;
            case 'p': port = std::atoi(optarg); break;
            case 'n': count = std::atoi(optarg); break;
            case 'm': message = optarg; break;
            case 'c': concurrency = std::atoi(optarg); break;
            case 'd': delay_ms = std::atoi(optarg); break;
            case 'q': quiet = true; break;
            default:
                std::printf("Usage: %s [-H host] [-p port] [-m message] [-n requests] "
                            "[-c concurrent_clients] [-d delay_ms] [-q]\n", argv[0]);
                return c == 'h' ? 0 : 1;
        }
    }
    if (count < 1 || concurrency < 1) { std::fprintf(stderr, "Invalid -n / -c\n"); return 1; }

    if (message.empty()) {
        std::printf("Enter request: ");
        std::fflush(stdout);
        if (!std::getline(std::cin, message) || message.empty()) message = "Hello";
        std::printf("\nResponse:\n");
    }

    std::vector<std::thread> threads;
    for (int t = 1; t <= concurrency; ++t)
        threads.emplace_back(run_requests, host, port, message, count, delay_ms, quiet,
                             concurrency > 1 ? t : 0);
    for (std::thread& t : threads) t.join();

    if (count * concurrency > 1 || quiet) {
        std::printf("--- Summary: %d sent, %d ok, %d failed ---\n", g_ok + g_failed, g_ok, g_failed);
        for (const auto& kv : g_per_server)
            std::printf("Backend Server %d : %d\n", kv.first, kv.second);
    }
    return g_failed == 0 ? 0 : 2;
}

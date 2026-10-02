// lbctl.cpp - Command-line client for the load balancer's admin channel and /proc monitoring.
//   lbctl ping | status | check | sys | proc <pid> | find <exe-name> [arg1]
// It uses exactly the same code as the GUI, so it is also a quick way to test the GUI's data source.
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <unistd.h>
#include "admin_client.h"
#include "proc_monitor.h"

int main(int argc, char** argv) {
    std::string sock = kDefaultAdminSocket, cmd;
    int i = 1;
    if (i + 1 < argc && std::string(argv[i]) == "-s") { sock = argv[i + 1]; i += 2; }
    if (i >= argc) {
        std::printf("Usage: %s [-s socket] ping|status|check|sys|proc <pid>|find <exe> [arg1]\n", argv[0]);
        return 1;
    }
    cmd = argv[i];
    std::string reply;

    if (cmd == "ping" || cmd == "check") {
        if (!admin_request(sock, cmd == "ping" ? "PING" : "CHECK", reply)) { std::printf("load balancer not running\n"); return 2; }
        std::fputs(reply.c_str(), stdout);
        return 0;
    }
    if (cmd == "status") {
        LbStatus st;
        if (!admin_request(sock, "STATUS", reply) || !parse_status(reply, st)) { std::printf("load balancer not running\n"); return 2; }
        std::printf("pid=%d port=%d algorithm=%s workers=%d uptime=%lds\n", st.pid, st.port, st.algorithm.c_str(), st.workers, st.uptime);
        std::printf("total=%llu success=%llu failed=%llu active_clients=%d\n", (unsigned long long)st.total,
                    (unsigned long long)st.success, (unsigned long long)st.failed, st.active_clients);
        std::printf("healthy=%d unhealthy=%d recovering=%d\n", st.healthy, st.unhealthy, st.recovering);
        for (const BackendInfo& b : st.backends)
            std::printf("  Server %d %s:%d %-10s active=%d total=%llu ok=%llu failed=%llu\n", b.id, b.ip.c_str(), b.port,
                        b.status.c_str(), b.active, (unsigned long long)b.total, (unsigned long long)b.ok, (unsigned long long)b.failed);
        return 0;
    }
    ProcMonitor mon;
    if (cmd == "sys") {
        SysInfo s; mon.sample_system(s); usleep(500000); mon.sample_system(s);
        std::printf("cpu=%.1f%% mem=%ld/%ld MB load1=%.2f cpus=%d\n", s.cpu_percent, (s.mem_total_kb - s.mem_available_kb) / 1024,
                    s.mem_total_kb / 1024, s.load1, s.cpus);
        return 0;
    }
    if (cmd == "proc" && i + 1 < argc) {
        ProcInfo p; pid_t pid = static_cast<pid_t>(std::atoi(argv[i + 1]));
        mon.sample_process(pid, p); usleep(500000);
        if (!mon.sample_process(pid, p)) { std::printf("no such process\n"); return 2; }
        std::printf("pid=%d cpu=%.1f%% rss=%ld KB threads=%d\n", p.pid, p.cpu_percent, p.rss_kb, p.threads);
        return 0;
    }
    if (cmd == "find" && i + 1 < argc) {
        for (pid_t pid : ProcMonitor::find_pids(argv[i + 1], i + 2 < argc ? argv[i + 2] : "")) std::printf("%d\n", pid);
        return 0;
    }
    std::fprintf(stderr, "bad command\n");
    return 1;
}

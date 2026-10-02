// Integration test for the GUI controller: every GUI button maps to a Controller method, and this
// test drives them against the REAL load_balancer / backend_server processes (no display needed).
#include <cstdio>
#include <functional>
#include <string>
#include <unistd.h>
#include "gui_controller.h"
#include "proc_monitor.h"

static int g_pass = 0, g_fail = 0;
static void check(const char* what, bool cond) {
    std::printf("  %s: %s\n", cond ? "PASS" : "FAIL", what);
    (cond ? g_pass : g_fail)++;
}
static bool wait_until(Controller& c, const std::function<bool(const ViewModel&)>& pred, int timeout_ms) {
    for (int waited = 0; waited <= timeout_ms; waited += 200) {
        c.refresh();
        if (pred(c.view())) return true;
        usleep(200000);
    }
    return false;
}

int main(int argc, char** argv) {
    Controller c(argc > 1 ? argv[1] : ".");
    c.refresh();
    if (c.view().lb_running || !ProcMonitor::find_pids("load_balancer").empty()) {
        std::printf("A load balancer is already running; stop it first.\n");
        return 2;
    }
    std::printf("=== GUI controller tests (real processes) ===\n");
    check("config/servers.conf loaded (3 servers)", c.view().backends.size() == 3);

    c.start_all();                                               // [ Start ] button
    check("Start: balancer reports 3 HEALTHY backends",
          wait_until(c, [](const ViewModel& v) { return v.lb_running && v.lb.healthy == 3; }, 8000));

    for (int i = 0; i < 6; ++i) c.send_test_request();           // [ Send Request ] button
    wait_until(c, [](const ViewModel& v) { return v.lb.total >= 6; }, 2000);
    check("Send Request x6: balancer counted total=6 success=6",
          c.view().lb.total == 6 && c.view().lb.success == 6);
    check("Round robin: each server handled exactly 2",
          c.view().backends[0].total == 2 && c.view().backends[1].total == 2 && c.view().backends[2].total == 2);
    check("/proc: balancer RSS > 0 and >= 10 threads", c.view().lb_proc.valid && c.view().lb_proc.rss_kb > 0 && c.view().lb_proc.threads >= 10);
    check("/proc: system memory and per-backend process info", c.view().sys.valid && c.view().backends[0].proc_running && c.view().backends[0].proc.rss_kb > 0);

    c.stop_backend(1);                                           // row button: stop Server 2
    check("Stop Server 2: detected UNHEALTHY and its process is gone",
          wait_until(c, [](const ViewModel& v) { return v.backends[1].health == "UNHEALTHY" && !v.backends[1].proc_running; }, 8000));
    unsigned long long failed_before = c.view().lb.failed;
    for (int i = 0; i < 4; ++i) c.send_test_request();
    wait_until(c, [](const ViewModel& v) { return v.lb.total >= 10; }, 2000);
    check("Requests while Server 2 is down all succeed", c.view().lb.failed == failed_before && c.view().lb.success == 10);

    c.start_backend(1);                                          // row button: start Server 2
    check("Start Server 2: recovered to HEALTHY",
          wait_until(c, [](const ViewModel& v) { return v.backends[1].health == "HEALTHY"; }, 10000));

    c.health_check();                                            // [ Health Check ] button
    check("Health Check button answered by balancer", c.status_line().find("Health check started") != std::string::npos);

    std::vector<std::string> log;
    c.read_log_tail(200, log);
    bool found_start = false, found_unhealthy = false;
    for (const std::string& l : log) {
        if (l.find("Load balancer started") != std::string::npos) found_start = true;
        if (l.find("is UNHEALTHY") != std::string::npos) found_unhealthy = true;
    }
    check("View Logs: real log file contains start + UNHEALTHY events", found_start && found_unhealthy);

    c.stop_all();                                                // [ Stop ] button
    check("Stop: balancer and all backends are gone",
          wait_until(c, [](const ViewModel& v) { return !v.lb_running; }, 8000) &&
              ProcMonitor::find_pids("backend_server").empty() && ProcMonitor::find_pids("load_balancer").empty());

    std::printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

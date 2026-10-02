// gui_controller.h - Everything the GUI does that is NOT drawing.
//
// The controller is plain C++ (no X11/Cairo), so it is unit/integration-tested without a display.
// It connects the GUI to the REAL project:
//   * reads live data from the running balancer via the admin UNIX socket (admin_client)
//   * reads CPU/memory/threads from /proc (proc_monitor)
//   * starts/stops the real bin/load_balancer and bin/backend_server processes (fork + execv + signals)
//   * sends real TCP requests to the balancer and tails the real log file
#pragma once
#include <chrono>
#include <string>
#include <sys/types.h>
#include <vector>
#include "admin_client.h"
#include "proc_monitor.h"

struct ConfiguredServer { int id; std::string name, ip; int port; };

struct BackendView {
    ConfiguredServer cfg;
    std::string health;          // HEALTHY / UNHEALTHY / RECOVERING from the balancer, or "-" if it is not running
    int active = 0;
    unsigned long long total = 0, ok = 0, failed = 0;
    long last_check = 0;
    bool proc_running = false;   // a backend_server process for this id exists in /proc
    ProcInfo proc;               // CPU / RSS / threads of that process
};

struct ViewModel {
    bool lb_running = false;
    LbStatus lb;
    ProcInfo lb_proc;
    SysInfo sys;
    std::vector<BackendView> backends;
    std::vector<std::string> log_lines;
};

class Controller {
public:
    // project_root: directory containing bin/ config/ logs/ (the controller chdir()s there).
    explicit Controller(const std::string& project_root);
    ~Controller();

    bool load_config();                 // reads config/servers.conf
    void refresh();                     // pull fresh data (call about once per second)
    const ViewModel& view() const { return view_; }

    // ---- actions triggered by GUI buttons ----
    void start_all();                   // start missing backends, then the load balancer
    void stop_all();                    // SIGINT to balancer (graceful), SIGTERM to backends
    void health_check();                // admin command CHECK
    void send_test_request();           // real TCP request to the balancer's client port
    void start_backend(size_t index);
    void stop_backend(size_t index);
    void shutdown_children();           // on GUI exit: stop only the processes this GUI started

    std::string status_line() const;    // text for the bottom status bar
    bool read_log_tail(size_t max_lines, std::vector<std::string>& out) const;

private:
    pid_t spawn(const std::vector<std::string>& argv, const std::string& output_file);
    void reap_children();
    void set_message(const std::string& m);

    std::string root_;
    std::string admin_socket_;
    std::string log_path_ = "logs/load_balancer.log";
    std::vector<ConfiguredServer> servers_;
    ViewModel view_;
    ProcMonitor monitor_;
    std::vector<pid_t> children_;       // processes started by this GUI (for waitpid)
    std::string message_;
    std::chrono::steady_clock::time_point message_at_;
    int request_counter_ = 0;
};

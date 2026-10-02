#include "gui_controller.h"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "net_utils.h"

Controller::Controller(const std::string& project_root)
    : root_(project_root), admin_socket_(kDefaultAdminSocket) {
    if (!root_.empty() && chdir(root_.c_str()) != 0)
        set_message("Cannot enter project folder " + root_);       // relative paths (bin/, logs/) depend on it
    mkdir("logs", 0755);
    load_config();
}

Controller::~Controller() { shutdown_children(); }

void Controller::set_message(const std::string& m) {
    message_ = m;
    message_at_ = std::chrono::steady_clock::now();
}

std::string Controller::status_line() const {
    std::string base = view_.lb_running ? "Load balancer running" : "Load balancer stopped";
    double age = std::chrono::duration<double>(std::chrono::steady_clock::now() - message_at_).count();
    if (!message_.empty() && age < 8.0) return base + "  |  " + message_;
    return base;
}

bool Controller::load_config() {
    servers_.clear();
    std::ifstream in("config/servers.conf");
    std::string line;
    while (std::getline(in, line)) {
        size_t h = line.find('#');
        if (h != std::string::npos) line.erase(h);
        std::istringstream iss(line);
        std::string name, ip, extra;
        int port = 0;
        if (!(iss >> name >> ip >> port) || (iss >> extra) || port < 1 || port > 65535) continue;
        servers_.push_back({static_cast<int>(servers_.size()) + 1, name, ip, port});
    }
    return !servers_.empty();
}

pid_t Controller::spawn(const std::vector<std::string>& argv, const std::string& output_file) {
    pid_t pid = fork();                                   // new process = copy of the GUI ...
    if (pid < 0) return -1;
    if (pid == 0) {                                       // ... which immediately becomes another program
        int fd = open(output_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); if (fd > 2) close(fd); }
        int null = open("/dev/null", O_RDONLY);
        if (null >= 0) { dup2(null, STDIN_FILENO); if (null > 2) close(null); }
        std::vector<char*> args;
        for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        execv(args[0], args.data());                      // replaces the child with bin/backend_server etc.
        _exit(127);
    }
    children_.push_back(pid);
    return pid;
}

void Controller::reap_children() {
    for (size_t i = 0; i < children_.size();) {           // collect finished children (no zombies)
        int st;
        pid_t r = waitpid(children_[i], &st, WNOHANG);
        if (r == children_[i] || (r < 0 && errno == ECHILD)) children_.erase(children_.begin() + static_cast<long>(i));
        else ++i;
    }
}

void Controller::refresh() {
    reap_children();
    std::string reply;
    LbStatus st;
    view_.lb_running = admin_request(admin_socket_, "STATUS", reply) && parse_status(reply, st);
    view_.lb = view_.lb_running ? st : LbStatus();

    pid_t lb_pid = view_.lb_running ? view_.lb.pid : 0;
    if (lb_pid == 0) {
        std::vector<pid_t> p = ProcMonitor::find_pids("load_balancer");
        if (!p.empty()) lb_pid = p[0];
    }
    view_.lb_proc = ProcInfo();
    if (lb_pid > 0) monitor_.sample_process(lb_pid, view_.lb_proc);
    monitor_.sample_system(view_.sys);

    view_.backends.clear();
    for (const ConfiguredServer& cs : servers_) {
        BackendView bv;
        bv.cfg = cs;
        bv.health = "-";
        for (const BackendInfo& bi : view_.lb.backends) {
            if (bi.id != cs.id) continue;
            bv.health = bi.status; bv.active = bi.active; bv.total = bi.total; bv.ok = bi.ok;
            bv.failed = bi.failed; bv.last_check = bi.last_check;
        }
        std::vector<pid_t> pids = ProcMonitor::find_pids("backend_server", std::to_string(cs.id));
        if (!pids.empty()) { bv.proc_running = monitor_.sample_process(pids[0], bv.proc); }
        view_.backends.push_back(bv);
    }
    read_log_tail(40, view_.log_lines);
}

bool Controller::read_log_tail(size_t max_lines, std::vector<std::string>& out) const {
    out.clear();
    std::ifstream in(log_path_, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    std::streamoff start = size > 16384 ? size - 16384 : 0;      // only read the last 16 KB
    in.seekg(start);
    std::string chunk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::istringstream ss(chunk);
    std::string line;
    std::vector<std::string> all;
    if (start > 0) std::getline(ss, line);                       // drop the partial first line
    while (std::getline(ss, line)) all.push_back(line);
    size_t from = all.size() > max_lines ? all.size() - max_lines : 0;
    out.assign(all.begin() + static_cast<long>(from), all.end());
    return true;
}

void Controller::start_backend(size_t index) {
    if (index >= servers_.size()) return;
    const ConfiguredServer& cs = servers_[index];
    if (!ProcMonitor::find_pids("backend_server", std::to_string(cs.id)).empty()) { set_message("Server " + std::to_string(cs.id) + " is already running"); return; }
    pid_t pid = spawn({"bin/backend_server", std::to_string(cs.id), std::to_string(cs.port)}, "logs/gui_backend_" + std::to_string(cs.id) + ".out");
    set_message(pid > 0 ? "Started Server " + std::to_string(cs.id) + " (pid " + std::to_string(pid) + ")" : "Could not start Server " + std::to_string(cs.id));
}

void Controller::stop_backend(size_t index) {
    if (index >= servers_.size()) return;
    int id = servers_[index].id;
    std::vector<pid_t> pids = ProcMonitor::find_pids("backend_server", std::to_string(id));
    if (pids.empty()) { set_message("Server " + std::to_string(id) + " is not running"); return; }
    for (pid_t p : pids) kill(p, SIGTERM);                       // graceful: backend handles SIGTERM
    set_message("Stopped Server " + std::to_string(id) + " - the health check will mark it UNHEALTHY");
}

void Controller::start_all() {
    for (size_t i = 0; i < servers_.size(); ++i)
        if (ProcMonitor::find_pids("backend_server", std::to_string(servers_[i].id)).empty()) start_backend(i);
    bool lb_up = view_.lb_running || !ProcMonitor::find_pids("load_balancer").empty();
    if (lb_up) { set_message("Load balancer is already running"); return; }
    usleep(300000);                                              // let the backends open their ports first
    pid_t pid = spawn({"bin/load_balancer", "-i", "1000"}, "logs/gui_load_balancer.out");
    set_message(pid > 0 ? "Started load balancer (pid " + std::to_string(pid) + ") and backend servers" : "Could not start load balancer - did you run make?");
}

void Controller::stop_all() {
    std::vector<pid_t> lb = ProcMonitor::find_pids("load_balancer");
    for (pid_t p : lb) kill(p, SIGINT);                          // same as Ctrl+C: graceful shutdown + statistics
    int stopped = 0;
    for (const ConfiguredServer& cs : servers_)
        for (pid_t p : ProcMonitor::find_pids("backend_server", std::to_string(cs.id))) { kill(p, SIGTERM); ++stopped; }
    set_message("Stop requested (balancer: " + std::to_string(lb.size()) + ", backends: " + std::to_string(stopped) + ")");
}

void Controller::health_check() {
    std::string reply;
    if (admin_request(admin_socket_, "CHECK", reply)) set_message("Health check started on all backends");
    else set_message("Load balancer is not running");
}

void Controller::send_test_request() {
    if (!view_.lb_running) { set_message("Load balancer is not running"); return; }
    int fd = connect_with_timeout("127.0.0.1", view_.lb.port, 2000);
    if (fd < 0) { set_message(std::string("Request failed: ") + std::strerror(errno)); return; }
    std::string req = "GUI test request #" + std::to_string(++request_counter_), resp;
    bool ok = send_all(fd, req + "\n") && recv_line(fd, resp, 512, 3000);
    close(fd);
    while (!resp.empty() && (resp.back() == '\n' || resp.back() == '\r')) resp.pop_back();
    set_message(ok ? "Reply: " + resp : "Request failed (no reply)");
}

void Controller::shutdown_children() {
    std::vector<pid_t> mine = children_;
    for (pid_t p : mine) kill(p, p == mine.back() ? SIGINT : SIGTERM);
    for (int i = 0; i < 30 && !children_.empty(); ++i) { reap_children(); if (!children_.empty()) usleep(100000); }
}

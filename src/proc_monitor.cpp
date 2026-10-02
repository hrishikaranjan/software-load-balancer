#include "proc_monitor.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unistd.h>

bool ProcMonitor::sample_process(pid_t pid, ProcInfo& out) {
    out = ProcInfo();
    std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (!stat || !std::getline(stat, line)) return false;      // process is gone
    // Format: pid (comm) state ppid ... ; comm may contain spaces, so parse after the LAST ')'.
    size_t rp = line.rfind(')');
    if (rp == std::string::npos) return false;
    std::istringstream rest(line.substr(rp + 2));
    std::vector<std::string> tok;
    for (std::string t; rest >> t;) tok.push_back(t);
    if (tok.size() < 18) return false;
    // token index = field number - 3: utime=field14 -> 11, stime=field15 -> 12, num_threads=field20 -> 17
    unsigned long long ticks = std::strtoull(tok[11].c_str(), nullptr, 10) + std::strtoull(tok[12].c_str(), nullptr, 10);
    out.threads = std::atoi(tok[17].c_str());

    std::ifstream statm("/proc/" + std::to_string(pid) + "/statm");
    long size_pages = 0, rss_pages = 0;
    if (statm >> size_pages >> rss_pages) out.rss_kb = rss_pages * (sysconf(_SC_PAGESIZE) / 1024);

    auto now = std::chrono::steady_clock::now();
    auto it = prev_.find(pid);
    if (it != prev_.end()) {
        double dt = std::chrono::duration<double>(now - it->second.at).count();
        double hz = static_cast<double>(sysconf(_SC_CLK_TCK));
        if (dt > 0.05 && ticks >= it->second.ticks)
            out.cpu_percent = 100.0 * static_cast<double>(ticks - it->second.ticks) / (dt * hz);
    }
    prev_[pid] = {ticks, now};
    out.pid = pid;
    out.valid = true;
    return true;
}

bool ProcMonitor::sample_system(SysInfo& out) {
    out = SysInfo();
    out.cpus = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    std::ifstream stat("/proc/stat");
    std::string label;
    unsigned long long v[8] = {0};
    if (stat >> label && label == "cpu") {
        for (int i = 0; i < 8; ++i) stat >> v[i];
        unsigned long long total = 0;
        for (int i = 0; i < 8; ++i) total += v[i];
        unsigned long long idle = v[3] + v[4];                   // idle + iowait
        if (have_sys_ && total > prev_total_)
            out.cpu_percent = 100.0 * static_cast<double>((total - prev_total_) - (idle - prev_idle_)) /
                              static_cast<double>(total - prev_total_);
        prev_total_ = total; prev_idle_ = idle; have_sys_ = true;
    }
    std::ifstream mem("/proc/meminfo");
    std::string key, unit;
    long val;
    while (mem >> key >> val >> unit) {
        if (key == "MemTotal:") out.mem_total_kb = val;
        else if (key == "MemAvailable:") out.mem_available_kb = val;
    }
    std::ifstream la("/proc/loadavg");
    la >> out.load1;
    out.valid = out.mem_total_kb > 0;
    return out.valid;
}

std::vector<pid_t> ProcMonitor::find_pids(const std::string& exe_name, const std::string& first_arg) {
    std::vector<pid_t> result;
    DIR* d = opendir("/proc");
    if (!d) return result;
    while (dirent* e = readdir(d)) {
        if (!std::isdigit(static_cast<unsigned char>(e->d_name[0]))) continue;
        pid_t pid = static_cast<pid_t>(std::atoi(e->d_name));
        if (pid == getpid()) continue;
        std::ifstream cmd(std::string("/proc/") + e->d_name + "/cmdline", std::ios::binary);
        std::string raw((std::istreambuf_iterator<char>(cmd)), std::istreambuf_iterator<char>());
        if (raw.empty()) continue;                               // kernel thread or zombie
        std::vector<std::string> argv;
        std::istringstream rs(raw);
        for (std::string a; std::getline(rs, a, '\0');) argv.push_back(a);
        if (argv.empty()) continue;
        size_t slash = argv[0].rfind('/');
        std::string base = (slash == std::string::npos) ? argv[0] : argv[0].substr(slash + 1);
        if (base != exe_name) continue;
        if (!first_arg.empty() && (argv.size() < 2 || argv[1] != first_arg)) continue;
        result.push_back(pid);
    }
    closedir(d);
    return result;
}

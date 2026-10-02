// proc_monitor.h - CPU / memory / thread monitoring by reading the Linux /proc pseudo-filesystem.
//   /proc/<pid>/stat    utime+stime (CPU ticks), number of threads
//   /proc/<pid>/statm   resident set size (pages)
//   /proc/<pid>/cmdline used to find processes by name
//   /proc/stat          system-wide CPU time
//   /proc/meminfo       total / available memory
//   /proc/loadavg       1-minute load average
#pragma once
#include <chrono>
#include <map>
#include <string>
#include <sys/types.h>
#include <vector>

struct ProcInfo {
    bool valid = false;
    pid_t pid = 0;
    double cpu_percent = 0.0;   // of ONE core, averaged since the previous sample of this pid
    long rss_kb = 0;            // resident memory
    int threads = 0;
};

struct SysInfo {
    bool valid = false;
    double cpu_percent = 0.0;   // whole machine, since previous sample
    long mem_total_kb = 0, mem_available_kb = 0;
    double load1 = 0.0;
    int cpus = 1;
};

class ProcMonitor {
public:
    bool sample_process(pid_t pid, ProcInfo& out);
    bool sample_system(SysInfo& out);
    // Scans /proc/*/cmdline: processes whose argv[0] basename == exe_name (and argv[1] == first_arg if given).
    static std::vector<pid_t> find_pids(const std::string& exe_name, const std::string& first_arg = "");

private:
    struct Prev { unsigned long long ticks; std::chrono::steady_clock::time_point at; };
    std::map<pid_t, Prev> prev_;
    bool have_sys_ = false;
    unsigned long long prev_total_ = 0, prev_idle_ = 0;
};

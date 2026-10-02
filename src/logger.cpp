#include "logger.h"
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <sys/stat.h>

namespace {
std::mutex g_log_mutex;          // serialises all writers (many threads log concurrently)
FILE* g_log_file = nullptr;
bool g_echo = true;

const char* level_name(LogLevel l) {
    switch (l) {
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR:   return "ERROR";
        case LogLevel::DEBUG:   return "DEBUG";
    }
    return "?";
}

// mkdir -p for the directory part of a path (uses the mkdir(2) system call).
void make_parent_dirs(const std::string& path) {
    size_t pos = 0;
    while ((pos = path.find('/', pos + 1)) != std::string::npos)
        mkdir(path.substr(0, pos).c_str(), 0755);   // EEXIST is fine
}
}  // namespace

bool logger_init(const std::string& path, bool echo_to_console) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_echo = echo_to_console;
    make_parent_dirs(path);
    g_log_file = std::fopen(path.c_str(), "a");   // append: keep history across runs
    return g_log_file != nullptr;
}

void logger_close() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log_file) { std::fclose(g_log_file); g_log_file = nullptr; }
}

void log_message(LogLevel level, const char* fmt, ...) {
    char body[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(body, sizeof body, fmt, args);
    va_end(args);

    char stamp[32];
    time_t now = std::time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);                    // re-entrant version for threads
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm_buf);

    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log_file) {
        std::fprintf(g_log_file, "[%s] [%s] %s\n", stamp, level_name(level), body);
        std::fflush(g_log_file);                   // survive crashes / kill -9
    }
    if (g_echo) {
        std::printf("[%s] [%s] %s\n", stamp, level_name(level), body);
        std::fflush(stdout);
    }
}

void log_raw(const std::string& text) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log_file) { std::fputs(text.c_str(), g_log_file); std::fflush(g_log_file); }
    if (g_echo)     { std::fputs(text.c_str(), stdout);     std::fflush(stdout); }
}

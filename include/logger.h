// logger.h - Thread-safe file + console logger (plain C stdio, one mutex).
#pragma once
#include <string>

enum class LogLevel { INFO, WARNING, ERROR, DEBUG };

// Opens (appends to) the log file; creates parent directories. Returns false on failure
// (console logging still works).
bool logger_init(const std::string& path, bool echo_to_console);
void logger_close();
void log_message(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
// Writes a pre-formatted block (e.g. the statistics table) without a timestamp.
void log_raw(const std::string& text);

#define LOG_INFO(...)  log_message(LogLevel::INFO,    __VA_ARGS__)
#define LOG_WARN(...)  log_message(LogLevel::WARNING, __VA_ARGS__)
#define LOG_ERROR(...) log_message(LogLevel::ERROR,   __VA_ARGS__)

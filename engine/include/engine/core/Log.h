#pragma once
// Minimal, dependency-free logging system. Logs to stdout/OutputDebugString
// and keeps a ring buffer that the editor's "Console" panel reads from.

#include "engine/core/Base.h"
#include <mutex>
#include <deque>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdarg>

namespace fw {

enum class LogLevel { Trace, Info, Warn, Error };

struct LogEntry {
    LogLevel level;
    std::string message;
};

class Log {
public:
    static Log& Get() {
        static Log instance;
        return instance;
    }

    void Write(LogLevel level, const char* fmt, ...) {
        char buf[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Entries.push_back({level, std::string(buf)});
        if (m_Entries.size() > 2000) m_Entries.pop_front();

        const char* prefix = "[INFO] ";
        switch (level) {
            case LogLevel::Trace: prefix = "[TRACE]"; break;
            case LogLevel::Info:  prefix = "[INFO] "; break;
            case LogLevel::Warn:  prefix = "[WARN] "; break;
            case LogLevel::Error: prefix = "[ERROR]"; break;
        }
        std::fprintf(level == LogLevel::Error ? stderr : stdout, "%s %s\n", prefix, buf);
    }

    std::vector<LogEntry> Snapshot() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return std::vector<LogEntry>(m_Entries.begin(), m_Entries.end());
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Entries.clear();
    }

private:
    std::mutex m_Mutex;
    std::deque<LogEntry> m_Entries;
};

} // namespace fw

#define FW_LOG_TRACE(...) ::fw::Log::Get().Write(::fw::LogLevel::Trace, __VA_ARGS__)
#define FW_LOG_INFO(...)  ::fw::Log::Get().Write(::fw::LogLevel::Info,  __VA_ARGS__)
#define FW_LOG_WARN(...)  ::fw::Log::Get().Write(::fw::LogLevel::Warn,  __VA_ARGS__)
#define FW_LOG_ERROR(...) ::fw::Log::Get().Write(::fw::LogLevel::Error, __VA_ARGS__)

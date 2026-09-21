#pragma once
// Minimal, dependency-free logging system. Logs to stdout/OutputDebugString
// and keeps a ring buffer that the editor's "Console" panel reads from.

#include "engine/core/Base.h"
#include <mutex>
#include <deque>
#include <string>
#include <vector>
#include <fstream>
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

    // Duplicates every message into a file. A Windows GUI-subsystem app has no
    // console attached, so without this the log is invisible - which made a
    // blank editor window impossible to diagnose from a user's machine.
    // Call Log::SetFile() (Application does it automatically) and read the file.
    void SetFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_FilePath = path;
        if (m_File.is_open()) m_File.close();
        if (!path.empty()) {
            m_File.open(path, std::ios::out | std::ios::trunc);
            m_File << "---- Forgeworks log ----\n";
            m_File.flush();
        }
    }

    const std::string& FilePath() const { return m_FilePath; }

    // Echoes messages to stdout/stderr as well (useful with --console or when a
    // debugger is attached). Windows GUI apps have no console by default, in
    // which case stdio output is simply discarded by the OS.
    void SetConsoleEcho(bool enabled) { m_ConsoleEcho = enabled; }

    static const char* LevelName(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Warn:  return "WARN";
            case LogLevel::Error: return "ERROR";
            default:              return "INFO";
        }
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
        if (m_ConsoleEcho)
            std::fprintf(level == LogLevel::Error ? stderr : stdout, "%s %s\n", prefix, buf);

        if (m_File.is_open()) {
            m_File << prefix << ' ' << buf << '\n';
            m_File.flush(); // flush every line: a crash must not lose the tail
        }
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
    std::ofstream m_File;
    std::string m_FilePath;
    bool m_ConsoleEcho = true;
};

} // namespace fw

#define FW_LOG_TRACE(...) ::fw::Log::Get().Write(::fw::LogLevel::Trace, __VA_ARGS__)
#define FW_LOG_INFO(...)  ::fw::Log::Get().Write(::fw::LogLevel::Info,  __VA_ARGS__)
#define FW_LOG_WARN(...)  ::fw::Log::Get().Write(::fw::LogLevel::Warn,  __VA_ARGS__)
#define FW_LOG_ERROR(...) ::fw::Log::Get().Write(::fw::LogLevel::Error, __VA_ARGS__)

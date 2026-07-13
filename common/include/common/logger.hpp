#pragma once

#include "common/concurrency.hpp"
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <memory>
#include <ctime>

namespace aether {
namespace log_detail {

inline std::string GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    
    // Using standard std::localtime which is thread-safe because we only call it
    // from Log() which holds GetLogMutex() lock.
    std::tm* timeinfo = std::localtime(&in_time_t);
    if (timeinfo) {
        ss << std::put_time(timeinfo, "%Y-%m-%d %H:%M:%S");
    } else {
        ss << "0000-00-00 00:00:00";
    }
    return ss.str();
}

inline Mutex& GetLogMutex() {
    static Mutex mtx;
    return mtx;
}

// Formatting recursion bases
inline void FormatImpl(std::stringstream& ss, const char* format) {
    ss << format;
}

template<typename T, typename... Args>
void FormatImpl(std::stringstream& ss, const char* format, const T& first, const Args&... args) {
    while (*format) {
        if (*format == '{' && *(format + 1) == '}') {
            ss << first;
            FormatImpl(ss, format + 2, args...);
            return;
        }
        ss << *format;
        format++;
    }
}

template<typename... Args>
std::string Format(const char* format, const Args&... args) {
    std::stringstream ss;
    FormatImpl(ss, format, args...);
    return ss.str();
}

inline void Log(const std::string& level, const std::string& msg) {
    LockGuard lock(GetLogMutex());
    std::cout << "[" << GetCurrentTimestamp() << "] [aether] [" << level << "] " << msg << std::endl;
}

} // namespace log_detail
} // namespace aether

namespace spdlog {

enum class level {
    debug,
    info,
    warn,
    error,
    critical
};

inline void set_level(level l) {}
inline void set_pattern(const std::string& p) {}

template<typename... Args>
void info(const char* fmt, const Args&... args) {
    aether::log_detail::Log("INFO", aether::log_detail::Format(fmt, args...));
}

template<typename... Args>
void debug(const char* fmt, const Args&... args) {
    aether::log_detail::Log("DEBUG", aether::log_detail::Format(fmt, args...));
}

template<typename... Args>
void warn(const char* fmt, const Args&... args) {
    aether::log_detail::Log("WARN", aether::log_detail::Format(fmt, args...));
}

template<typename... Args>
void error(const char* fmt, const Args&... args) {
    aether::log_detail::Log("ERROR", aether::log_detail::Format(fmt, args...));
}

template<typename... Args>
void critical(const char* fmt, const Args&... args) {
    aether::log_detail::Log("CRITICAL", aether::log_detail::Format(fmt, args...));
}

class logger {
public:
    template<typename... Args>
    void info(const char* fmt, const Args&... args) { spdlog::info(fmt, args...); }
};

inline std::shared_ptr<logger> get(const std::string& name) {
    return nullptr;
}

inline std::shared_ptr<logger> stdout_color_mt(const std::string& name) {
    return std::make_shared<logger>();
}

inline void set_default_logger(std::shared_ptr<logger> l) {}

} // namespace spdlog

namespace aether {
inline void InitializeLogger() {
    // Already set up inline, no-op
}
} // namespace aether

#include <loom/log.hpp>
#include <cstdarg>
#include <cstdio>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

namespace loom::log {

static Level s_level = Info;
static bool s_color_initialized = false;
static bool s_color_enabled = false;

static void init_color() {
    if (!s_color_initialized) {
        s_color_enabled = isatty(fileno(stderr));
        s_color_initialized = true;
    }
}

void set_level(Level lvl) {
    s_level = lvl;
}

Level get_level() {
    return s_level;
}

void set_color_enabled(bool enabled) {
    s_color_enabled = enabled;
    s_color_initialized = true;
}

bool is_color_enabled() {
    init_color();
    return s_color_enabled;
}

const char* level_name(Level lvl) {
    switch (lvl) {
        case Trace: return "trace";
        case Debug: return "debug";
        case Info:  return "info";
        case Warn:  return "warn";
        case Error: return "error";
    }
    return "unknown";
}

static const char* level_color(Level lvl) {
    switch (lvl) {
        case Trace: return "\033[90m";   // gray
        case Debug: return "\033[36m";   // cyan
        case Info:  return "\033[32m";   // green
        case Warn:  return "\033[33m";   // yellow
        case Error: return "\033[31m";   // red
    }
    return "";
}

static const char* reset_color() {
    return "\033[0m";
}

static void log_message(Level lvl, const char* fmt, va_list args) {
    if (lvl < s_level) return;
    init_color();

    if (s_color_enabled) {
        std::fprintf(stderr, "%s%s%s: ", level_color(lvl), level_name(lvl), reset_color());
    } else {
        std::fprintf(stderr, "%s: ", level_name(lvl));
    }

    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
}

void trace(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(Trace, fmt, args);
    va_end(args);
}

void debug(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(Debug, fmt, args);
    va_end(args);
}

void info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(Info, fmt, args);
    va_end(args);
}

void warn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(Warn, fmt, args);
    va_end(args);
}

void error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(Error, fmt, args);
    va_end(args);
}

} // namespace loom::log

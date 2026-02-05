#pragma once

#include <string>
#include <cstdio>

namespace loom::log {

enum Level { Trace, Debug, Info, Warn, Error };

void set_level(Level lvl);
Level get_level();

void set_color_enabled(bool enabled);
bool is_color_enabled();

void trace(const char* fmt, ...);
void debug(const char* fmt, ...);
void info(const char* fmt, ...);
void warn(const char* fmt, ...);
void error(const char* fmt, ...);

// Returns the name string for a level
const char* level_name(Level lvl);

} // namespace loom::log

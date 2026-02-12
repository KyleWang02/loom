#pragma once

#include <string>
#include <filesystem>

namespace loom {

/// RAII file lock using flock(). Acquires exclusive lock on construction,
/// releases on destruction. Used for concurrent build safety.
class FileLock {
public:
    explicit FileLock(const std::filesystem::path& lock_path);
    ~FileLock();
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

    bool is_locked() const;

private:
    int fd_ = -1;
    std::filesystem::path path_;
};

/// Simple progress indicator for long operations.
/// Prints status messages to stderr.
class Progress {
public:
    explicit Progress(const std::string& label, bool enabled = true);
    ~Progress();

    void update(const std::string& message);
    void finish(const std::string& message = "done");

private:
    std::string label_;
    bool enabled_;
    bool finished_ = false;
};

/// Install signal handlers for graceful cleanup on SIGINT/SIGTERM.
/// The handler sets a global flag that can be checked with is_interrupted().
void install_signal_handlers();

/// Check if an interrupt signal has been received.
bool is_interrupted();

/// Reset the interrupt flag (e.g., after handling it).
void reset_interrupt();

} // namespace loom

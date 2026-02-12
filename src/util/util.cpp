#include "loom/util.hpp"
#include "loom/log.hpp"

#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <atomic>
#include <iostream>
#include <cerrno>
#include <cstring>

namespace loom {

// ---------------------------------------------------------------------------
// FileLock
// ---------------------------------------------------------------------------

FileLock::FileLock(const std::filesystem::path& lock_path)
    : path_(lock_path)
{
    // Create parent directories if they don't exist.
    std::error_code ec;
    if (path_.has_parent_path()) {
        std::filesystem::create_directories(path_.parent_path(), ec);
        if (ec) {
            loom::log::warn("failed to create lock dir %s: %s",
                            path_.parent_path().c_str(), ec.message().c_str());
            return;
        }
    }

    fd_ = ::open(path_.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd_ < 0) {
        loom::log::warn("failed to open lock file %s: %s",
                        path_.c_str(), std::strerror(errno));
        fd_ = -1;
        return;
    }

    // Try non-blocking lock first.
    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) {
            loom::log::warn("lock file %s held by another process, waiting...",
                            path_.c_str());
            // Fall back to blocking lock.
            if (::flock(fd_, LOCK_EX) != 0) {
                loom::log::warn("failed to acquire lock %s: %s",
                                path_.c_str(), std::strerror(errno));
                ::close(fd_);
                fd_ = -1;
            }
        } else {
            loom::log::warn("failed to acquire lock %s: %s",
                            path_.c_str(), std::strerror(errno));
            ::close(fd_);
            fd_ = -1;
        }
    }
}

FileLock::~FileLock() {
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
    }
}

bool FileLock::is_locked() const {
    return fd_ != -1;
}

// ---------------------------------------------------------------------------
// Progress
// ---------------------------------------------------------------------------

Progress::Progress(const std::string& label, bool enabled)
    : label_(label)
    , enabled_(enabled && ::isatty(STDERR_FILENO))
{
    if (enabled_) {
        std::cerr << "[" << label_ << "] ";
        std::cerr.flush();
    }
}

Progress::~Progress() {
    if (!finished_) {
        finish();
    }
}

void Progress::update(const std::string& message) {
    if (!enabled_) return;
    std::cerr << "\r[" << label_ << "] " << message;
    std::cerr.flush();
}

void Progress::finish(const std::string& message) {
    if (finished_) return;
    finished_ = true;
    if (!enabled_) return;
    std::cerr << "\r[" << label_ << "] " << message << "\n";
    std::cerr.flush();
}

// ---------------------------------------------------------------------------
// Signal handling
// ---------------------------------------------------------------------------

static std::atomic<bool> g_interrupted{false};

static void signal_handler(int /*signum*/) {
    g_interrupted.store(true, std::memory_order_relaxed);
}

void install_signal_handlers() {
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0;
    ::sigemptyset(&sa.sa_mask);

    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
}

bool is_interrupted() {
    return g_interrupted.load(std::memory_order_relaxed);
}

void reset_interrupt() {
    g_interrupted.store(false, std::memory_order_relaxed);
}

} // namespace loom

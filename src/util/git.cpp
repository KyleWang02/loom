#include <loom/git.hpp>
#include <loom/log.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <unordered_map>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace loom {

// ---------------------------------------------------------------------------
// Subprocess
// ---------------------------------------------------------------------------

Result<CommandResult> run_command(const std::vector<std::string>& args,
                                  const std::string& working_dir,
                                  int timeout_seconds) {
    if (args.empty()) {
        return LoomError{LoomError::InvalidArg, "run_command: empty args"};
    }

    // Build argv for execvp
    std::vector<const char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);

    int stdout_pipe[2];
    int stderr_pipe[2];

    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        return LoomError{LoomError::IO,
            std::string("pipe() failed: ") + strerror(errno)};
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return LoomError{LoomError::IO,
            std::string("fork() failed: ") + strerror(errno)};
    }

    if (pid == 0) {
        // Child process
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        if (!working_dir.empty()) {
            if (chdir(working_dir.c_str()) != 0) {
                _exit(127);
            }
        }

        execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127);  // execvp failed
    }

    // Parent process
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    // Set non-blocking on read ends
    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

    std::string out_buf, err_buf;
    char buf[4096];
    bool done = false;
    auto start = std::chrono::steady_clock::now();

    while (!done) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count()
                >= timeout_seconds) {
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            return LoomError{LoomError::IO,
                "command timed out after " + std::to_string(timeout_seconds) + "s"};
        }

        // Read available data
        ssize_t n;
        while ((n = read(stdout_pipe[0], buf, sizeof(buf))) > 0) {
            out_buf.append(buf, static_cast<size_t>(n));
        }
        while ((n = read(stderr_pipe[0], buf, sizeof(buf))) > 0) {
            err_buf.append(buf, static_cast<size_t>(n));
        }

        int status = 0;
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            // Drain remaining data
            while ((n = read(stdout_pipe[0], buf, sizeof(buf))) > 0) {
                out_buf.append(buf, static_cast<size_t>(n));
            }
            while ((n = read(stderr_pipe[0], buf, sizeof(buf))) > 0) {
                err_buf.append(buf, static_cast<size_t>(n));
            }

            close(stdout_pipe[0]);
            close(stderr_pipe[0]);

            int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            return Result<CommandResult>::ok(
                CommandResult{exit_code, std::move(out_buf), std::move(err_buf)});
        } else if (w < 0) {
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            return LoomError{LoomError::IO,
                std::string("waitpid failed: ") + strerror(errno)};
        }

        // Brief sleep to avoid busy-wait
        usleep(1000);  // 1ms
    }

    // Unreachable, but satisfy compiler
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    return LoomError{LoomError::IO, "unexpected exit from run_command loop"};
}

// ---------------------------------------------------------------------------
// Tag parsing (pure functions)
// ---------------------------------------------------------------------------

static std::string strip_v_prefix(const std::string& tag) {
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) {
        return tag.substr(1);
    }
    return tag;
}

Result<std::vector<RemoteTag>> parse_ls_remote_tags(const std::string& ls_remote_output) {
    std::vector<RemoteTag> tags;
    std::istringstream stream(ls_remote_output);
    std::string line;

    // ls-remote output: "<sha>\trefs/tags/<tagname>"
    // Annotated tags have a deref line: "<sha>\trefs/tags/<tagname>^{}"
    // The ^{} line's SHA is the actual commit (preferred over lightweight SHA).

    // First pass: collect all tags
    struct RawTag {
        std::string sha;
        std::string name;
        bool is_deref;  // has ^{}
    };
    std::vector<RawTag> raw_tags;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        auto tab_pos = line.find('\t');
        if (tab_pos == std::string::npos) continue;

        std::string sha = line.substr(0, tab_pos);
        std::string ref = line.substr(tab_pos + 1);

        // Must be refs/tags/
        const std::string prefix = "refs/tags/";
        if (ref.substr(0, prefix.size()) != prefix) continue;

        std::string tag_name = ref.substr(prefix.size());
        bool is_deref = false;

        // Check for ^{} suffix (annotated tag deref)
        const std::string deref_suffix = "^{}";
        if (tag_name.size() > deref_suffix.size() &&
            tag_name.compare(tag_name.size() - deref_suffix.size(),
                              deref_suffix.size(), deref_suffix) == 0) {
            tag_name = tag_name.substr(0, tag_name.size() - deref_suffix.size());
            is_deref = true;
        }

        raw_tags.push_back({sha, tag_name, is_deref});
    }

    // Build final tags: deref SHA overrides lightweight SHA
    // Use a map to handle deref overrides
    std::unordered_map<std::string, std::string> tag_sha;
    for (const auto& rt : raw_tags) {
        auto it = tag_sha.find(rt.name);
        if (it == tag_sha.end()) {
            tag_sha[rt.name] = rt.sha;
        } else if (rt.is_deref) {
            // Deref line overrides
            it->second = rt.sha;
        }
    }

    for (const auto& [name, sha] : tag_sha) {
        std::string ver_str = strip_v_prefix(name);
        auto ver = Version::parse(ver_str);
        if (ver.is_err()) continue;  // skip non-semver tags

        tags.push_back(RemoteTag{name, sha, std::move(ver).value()});
    }

    // Sort by version descending (highest first)
    std::sort(tags.begin(), tags.end(),
              [](const RemoteTag& a, const RemoteTag& b) {
                  return a.version > b.version;
              });

    return Result<std::vector<RemoteTag>>::ok(std::move(tags));
}

Result<RemoteTag> resolve_version_from_tags(const std::vector<RemoteTag>& tags,
                                             const VersionReq& req) {
    // Tags are sorted descending, so first match is highest version
    for (const auto& tag : tags) {
        if (req.matches(tag.version)) {
            return Result<RemoteTag>::ok(tag);
        }
    }
    return LoomError{LoomError::Version,
        "no tag matches version requirement '" + req.to_string() + "'"};
}

// ---------------------------------------------------------------------------
// GitCli
// ---------------------------------------------------------------------------

Result<std::string> GitCli::check_version() {
    auto r = run_command({"git", "--version"}, "", timeout_seconds_);
    if (r.is_err()) return std::move(r).error();

    auto& cmd = r.value();
    if (cmd.exit_code != 0) {
        return LoomError{LoomError::NotFound,
            "git not found or failed", "install git >= 2.20"};
    }

    // Parse "git version X.Y.Z..."
    std::string out = cmd.stdout_str;
    // Trim trailing whitespace
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
        out.pop_back();
    }

    // Extract version number
    auto pos = out.find("git version ");
    if (pos == std::string::npos) {
        return LoomError{LoomError::Parse,
            "unexpected git --version output: " + out};
    }
    std::string ver_str = out.substr(pos + 12);

    // Parse major.minor
    int major = 0, minor = 0;
    if (sscanf(ver_str.c_str(), "%d.%d", &major, &minor) < 2) {
        return LoomError{LoomError::Parse,
            "cannot parse git version: " + ver_str};
    }

    if (major < 2 || (major == 2 && minor < 20)) {
        return LoomError{LoomError::Version,
            "git version " + ver_str + " too old",
            "upgrade to git >= 2.20"};
    }

    return Result<std::string>::ok(std::move(ver_str));
}

Result<std::string> GitCli::ls_remote(const std::string& url) {
    if (offline_) {
        return LoomError{LoomError::Network,
            "cannot ls-remote in offline mode", "run without --offline"};
    }

    loom::log::debug("git ls-remote --tags --refs %s", url.c_str());
    auto r = run_command({"git", "ls-remote", "--tags", "--refs", url},
                          "", timeout_seconds_);
    if (r.is_err()) return std::move(r).error();

    auto& cmd = r.value();
    if (cmd.exit_code != 0) {
        return LoomError{LoomError::Network,
            "git ls-remote failed: " + cmd.stderr_str};
    }

    return Result<std::string>::ok(std::move(cmd.stdout_str));
}

Result<std::string> GitCli::clone_bare(const std::string& url,
                                        const std::string& dest) {
    if (offline_) {
        return LoomError{LoomError::Network,
            "cannot clone in offline mode", "run without --offline"};
    }

    loom::log::debug("git clone --bare %s %s", url.c_str(), dest.c_str());
    auto r = run_command({"git", "clone", "--bare", url, dest},
                          "", timeout_seconds_);
    if (r.is_err()) return std::move(r).error();

    auto& cmd = r.value();
    if (cmd.exit_code != 0) {
        return LoomError{LoomError::Network,
            "git clone --bare failed: " + cmd.stderr_str};
    }

    return Result<std::string>::ok(dest);
}

Status GitCli::fetch(const std::string& bare_repo_path) {
    if (offline_) {
        return LoomError{LoomError::Network,
            "cannot fetch in offline mode", "run without --offline"};
    }

    loom::log::debug("git -C %s fetch --all --tags", bare_repo_path.c_str());
    auto r = run_command({"git", "-C", bare_repo_path, "fetch", "--all", "--tags"},
                          "", timeout_seconds_);
    if (r.is_err()) return std::move(r).error();

    auto& cmd = r.value();
    if (cmd.exit_code != 0) {
        return LoomError{LoomError::Network,
            "git fetch failed: " + cmd.stderr_str};
    }

    return ok_status();
}

Result<std::string> GitCli::checkout(const std::string& bare_repo,
                                      const std::string& commit,
                                      const std::string& dest) {
    loom::log::debug("git clone --shared %s %s", bare_repo.c_str(), dest.c_str());

    // Clone from bare repo using --shared for speed
    auto r = run_command({"git", "clone", "--shared", bare_repo, dest},
                          "", timeout_seconds_);
    if (r.is_err()) return std::move(r).error();

    if (r.value().exit_code != 0) {
        return LoomError{LoomError::IO,
            "git clone --shared failed: " + r.value().stderr_str};
    }

    // Checkout the specific commit
    loom::log::debug("git -C %s checkout %s", dest.c_str(), commit.c_str());
    auto r2 = run_command({"git", "-C", dest, "checkout", commit},
                           "", timeout_seconds_);
    if (r2.is_err()) return std::move(r2).error();

    if (r2.value().exit_code != 0) {
        return LoomError{LoomError::NotFound,
            "git checkout failed: " + r2.value().stderr_str};
    }

    return Result<std::string>::ok(dest);
}

Result<std::string> GitCli::resolve_ref(const std::string& bare_repo,
                                          const std::string& ref) {
    auto r = run_command({"git", "-C", bare_repo, "rev-parse", ref},
                          "", timeout_seconds_);
    if (r.is_err()) return std::move(r).error();

    auto& cmd = r.value();
    if (cmd.exit_code != 0) {
        return LoomError{LoomError::NotFound,
            "cannot resolve ref '" + ref + "': " + cmd.stderr_str};
    }

    std::string sha = cmd.stdout_str;
    while (!sha.empty() && (sha.back() == '\n' || sha.back() == '\r')) {
        sha.pop_back();
    }

    return Result<std::string>::ok(std::move(sha));
}

Result<std::string> GitCli::show_file(const std::string& bare_repo,
                                       const std::string& commit,
                                       const std::string& filepath) {
    std::string rev_file = commit + ":" + filepath;
    auto r = run_command({"git", "-C", bare_repo, "show", rev_file},
                          "", timeout_seconds_);
    if (r.is_err()) return std::move(r).error();

    auto& cmd = r.value();
    if (cmd.exit_code != 0) {
        return LoomError{LoomError::NotFound,
            "cannot read '" + filepath + "' at " + commit + ": " + cmd.stderr_str};
    }

    return Result<std::string>::ok(std::move(cmd.stdout_str));
}

} // namespace loom

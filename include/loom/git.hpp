#pragma once

#include <loom/result.hpp>
#include <loom/version.hpp>
#include <string>
#include <vector>

namespace loom {

// Result of running an external command
struct CommandResult {
    int exit_code;
    std::string stdout_str;
    std::string stderr_str;
};

// Run an external command, capturing stdout and stderr.
// Returns error on fork/exec failure or timeout.
Result<CommandResult> run_command(const std::vector<std::string>& args,
                                  const std::string& working_dir = "",
                                  int timeout_seconds = 60);

// A parsed tag from `git ls-remote` output
struct RemoteTag {
    std::string name;      // e.g., "v1.3.0"
    std::string commit;    // full SHA
    Version version;       // parsed semver
};

// Parse `git ls-remote --tags --refs` output into structured tags.
// Skips tags that don't parse as semver (with or without "v" prefix).
Result<std::vector<RemoteTag>> parse_ls_remote_tags(const std::string& ls_remote_output);

// Find the best matching tag for a version requirement.
// Returns the highest version that satisfies the constraint.
Result<RemoteTag> resolve_version_from_tags(const std::vector<RemoteTag>& tags,
                                             const VersionReq& req);

// Wrapper around git CLI operations
class GitCli {
public:
    // Check git is available and version >= 2.20
    Result<std::string> check_version();

    // List remote tags: `git ls-remote --tags --refs <url>`
    Result<std::string> ls_remote(const std::string& url);

    // Clone as bare repo: `git clone --bare <url> <dest>`
    Result<std::string> clone_bare(const std::string& url, const std::string& dest);

    // Fetch all refs in a bare repo: `git -C <bare> fetch --all --tags`
    Status fetch(const std::string& bare_repo_path);

    // Checkout a commit from bare repo using --shared clone
    Result<std::string> checkout(const std::string& bare_repo,
                                  const std::string& commit,
                                  const std::string& dest);

    // Resolve a ref (tag, branch, SHA) to a full commit hash
    Result<std::string> resolve_ref(const std::string& bare_repo,
                                     const std::string& ref);

    // Read a file at a specific commit: `git show <commit>:<filepath>`
    Result<std::string> show_file(const std::string& bare_repo,
                                   const std::string& commit,
                                   const std::string& filepath);

    void set_timeout(int seconds) { timeout_seconds_ = seconds; }
    void set_offline(bool offline) { offline_ = offline; }
    bool is_offline() const { return offline_; }

private:
    int timeout_seconds_ = 60;
    bool offline_ = false;
};

} // namespace loom

#pragma once

#include <loom/result.hpp>
#include <loom/git.hpp>
#include <string>

namespace loom {

// Two-tier git cache: bare repos (db) + working tree checkouts
//
// Layout:
//   <root>/git/db/<name>-<hash>/           bare clone
//   <root>/git/checkouts/<name>-<hash>/<version>-<short_sha>/  working tree
class CacheManager {
public:
    explicit CacheManager(const std::string& cache_root);

    // Default: ~/.loom/cache
    static std::string default_cache_root();

    // Ensure a bare repo exists (clone or fetch)
    Result<std::string> ensure_bare_repo(const std::string& name,
                                          const std::string& url);

    // Ensure a checkout exists at the given version/commit
    Result<std::string> ensure_checkout(const std::string& name,
                                         const std::string& url,
                                         const std::string& version,
                                         const std::string& commit);

    // SHA-256 checksum of all files in a directory
    Result<std::string> compute_checksum(const std::string& checkout_path);

    // Remove all checkouts (keep bare repos)
    Status clean_checkouts();

    // Remove entire git cache
    Status clean_all();

    // Deterministic dir name: <pkg_name>-<SHA256(url)[0:16]>
    static std::string cache_dir_name(const std::string& pkg_name,
                                       const std::string& url);

    // Full path to bare repo
    std::string bare_repo_path(const std::string& pkg_name,
                                const std::string& url) const;

    // Full path to checkout
    std::string checkout_path(const std::string& pkg_name,
                               const std::string& url,
                               const std::string& version,
                               const std::string& commit) const;

    GitCli& git() { return git_; }
    const std::string& cache_root() const { return cache_root_; }

private:
    std::string cache_root_;
    GitCli git_;
};

} // namespace loom

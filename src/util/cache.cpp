#include <loom/cache.hpp>
#include <loom/sha256.hpp>
#include <loom/log.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace loom {

CacheManager::CacheManager(const std::string& cache_root)
    : cache_root_(cache_root) {}

std::string CacheManager::default_cache_root() {
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp";
    return std::string(home) + "/.loom/cache";
}

std::string CacheManager::cache_dir_name(const std::string& pkg_name,
                                          const std::string& url) {
    std::string hash = SHA256::hash_hex(url);
    return pkg_name + "-" + hash.substr(0, 16);
}

std::string CacheManager::bare_repo_path(const std::string& pkg_name,
                                          const std::string& url) const {
    return cache_root_ + "/git/db/" + cache_dir_name(pkg_name, url);
}

std::string CacheManager::checkout_path(const std::string& pkg_name,
                                         const std::string& url,
                                         const std::string& version,
                                         const std::string& commit) const {
    std::string short_sha = commit.substr(0, std::min<size_t>(commit.size(), 7));
    return cache_root_ + "/git/checkouts/" + cache_dir_name(pkg_name, url)
           + "/" + version + "-" + short_sha;
}

Result<std::string> CacheManager::ensure_bare_repo(const std::string& name,
                                                     const std::string& url) {
    std::string path = bare_repo_path(name, url);

    if (fs::exists(path)) {
        loom::log::debug("bare repo exists, fetching: %s", path.c_str());
        LOOM_TRY(git_.fetch(path));
        return Result<std::string>::ok(path);
    }

    // Create parent directory
    fs::create_directories(fs::path(path).parent_path());

    loom::log::info("cloning bare: %s -> %s", url.c_str(), path.c_str());
    return git_.clone_bare(url, path);
}

Result<std::string> CacheManager::ensure_checkout(const std::string& name,
                                                    const std::string& url,
                                                    const std::string& version,
                                                    const std::string& commit) {
    std::string co_path = checkout_path(name, url, version, commit);

    if (fs::exists(co_path)) {
        loom::log::debug("checkout exists: %s", co_path.c_str());
        return Result<std::string>::ok(co_path);
    }

    // Ensure bare repo is available
    auto bare = ensure_bare_repo(name, url);
    if (bare.is_err()) return std::move(bare).error();

    // Create parent directory
    fs::create_directories(fs::path(co_path).parent_path());

    loom::log::info("checking out %s@%s -> %s",
                    name.c_str(), version.c_str(), co_path.c_str());
    return git_.checkout(bare.value(), commit, co_path);
}

Result<std::string> CacheManager::compute_checksum(const std::string& checkout_path) {
    if (!fs::exists(checkout_path) || !fs::is_directory(checkout_path)) {
        return LoomError{LoomError::NotFound,
            "checkout path does not exist: " + checkout_path};
    }

    // Collect all regular file paths, sorted for determinism
    std::vector<std::string> file_paths;
    for (const auto& entry : fs::recursive_directory_iterator(checkout_path)) {
        if (!entry.is_regular_file()) continue;
        // Skip .git directory
        std::string rel = entry.path().string().substr(checkout_path.size());
        if (rel.find("/.git/") != std::string::npos ||
            rel.find("/.git") == 0) continue;
        file_paths.push_back(entry.path().string());
    }
    std::sort(file_paths.begin(), file_paths.end());

    // Hash all files together
    SHA256 hasher;
    for (const auto& fp : file_paths) {
        // Include relative path in hash for determinism
        std::string rel = fp.substr(checkout_path.size());
        hasher.update(rel);

        std::ifstream in(fp, std::ios::binary);
        if (!in) continue;
        char buf[8192];
        while (in.read(buf, sizeof(buf))) {
            hasher.update(reinterpret_cast<const uint8_t*>(buf),
                          static_cast<size_t>(in.gcount()));
        }
        if (in.gcount() > 0) {
            hasher.update(reinterpret_cast<const uint8_t*>(buf),
                          static_cast<size_t>(in.gcount()));
        }
    }

    return Result<std::string>::ok(SHA256::bytes_to_hex(hasher.finalize()));
}

Status CacheManager::clean_checkouts() {
    std::string co_dir = cache_root_ + "/git/checkouts";
    std::error_code ec;
    fs::remove_all(co_dir, ec);
    if (ec) {
        return LoomError{LoomError::IO,
            "failed to clean checkouts: " + ec.message()};
    }
    return ok_status();
}

Status CacheManager::clean_all() {
    std::string git_dir = cache_root_ + "/git";
    std::error_code ec;
    fs::remove_all(git_dir, ec);
    if (ec) {
        return LoomError{LoomError::IO,
            "failed to clean cache: " + ec.message()};
    }
    return ok_status();
}

} // namespace loom

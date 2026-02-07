#pragma once

#include <loom/result.hpp>
#include <loom/source.hpp>
#include <string>
#include <vector>

namespace loom {

struct LockedPackage {
    std::string name;
    std::string version;
    std::string source;          // "git+<url>" or "path+<path>"
    std::string commit;          // full SHA (empty for path sources)
    std::string ref;             // original tag/branch ref
    std::string checksum;        // SHA-256 of checkout tree
    std::vector<std::string> dependencies;  // names of deps
};

struct LockFile {
    std::string loom_version;
    std::string root_name;
    std::string root_version;
    std::vector<LockedPackage> packages;

    // Parse a Loom.lock file from disk
    static Result<LockFile> load(const std::string& path);

    // Write Loom.lock to disk (sorted, deterministic)
    Status save(const std::string& path) const;

    // Find a locked package by name (nullptr if not found)
    const LockedPackage* find(const std::string& name) const;

    // Check if the lockfile is stale relative to manifest dependencies
    bool is_stale(const std::vector<Dependency>& manifest_deps) const;
};

} // namespace loom

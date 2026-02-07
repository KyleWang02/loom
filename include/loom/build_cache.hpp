#pragma once

#include <loom/result.hpp>
#include <loom/lang/ir.hpp>
#include <memory>
#include <string>
#include <vector>

namespace loom {

struct CacheStats {
    int64_t file_stat_count = 0;
    int64_t parse_result_count = 0;
    int64_t include_dep_count = 0;
    int64_t dep_edge_count = 0;
    int64_t filelist_count = 0;
    int64_t total_bytes = 0;
};

struct FileStatEntry {
    std::string path;
    uint64_t inode = 0;
    int64_t mtime_sec = 0;
    int64_t mtime_nsec = 0;
    int64_t size = 0;
    std::string content_hash;
};

struct IncludeDepEntry {
    std::string source_hash;
    std::string include_path;
    std::string include_hash;
};

struct DepEdgeEntry {
    std::string source_hash;
    std::string source_unit;
    std::string target_unit;
};

struct FilelistCacheEntry {
    std::string filelist_key;
    std::vector<std::string> file_list;
    std::vector<std::string> top_modules;
    int64_t created_at = 0;
};

class BuildCache {
public:
    BuildCache();
    ~BuildCache();
    BuildCache(BuildCache&&) noexcept;
    BuildCache& operator=(BuildCache&&) noexcept;

    // Database lifecycle
    Status open(const std::string& db_path);
    void close();
    bool is_open() const;
    static std::string default_cache_path();

    // Stat cache
    Result<FileStatEntry> lookup_stat(const std::string& path);
    Status update_stat(const FileStatEntry& entry);
    Status remove_stat(const std::string& path);
    Result<std::string> cached_file_hash(const std::string& path);

    // Parse cache
    Result<ParseResult> lookup_parse(const std::string& content_hash);
    Status store_parse(const std::string& content_hash, const ParseResult& result);

    // Include dependency tracking
    Result<std::vector<IncludeDepEntry>> get_includes(const std::string& source_hash);
    Status store_includes(const std::string& source_hash,
                          const std::vector<IncludeDepEntry>& deps);
    Result<std::vector<std::string>> find_includers(const std::string& include_hash);

    // Dependency edge tracking
    Result<std::vector<DepEdgeEntry>> get_edges(const std::string& source_hash);
    Status store_edges(const std::string& source_hash,
                       const std::vector<DepEdgeEntry>& edges);

    // Filelist cache
    Result<FilelistCacheEntry> lookup_filelist(const std::string& filelist_key);
    Status store_filelist(const FilelistCacheEntry& entry);

    // Hash computation helpers
    static std::string compute_effective_hash(
        const std::string& content_hash,
        const std::vector<std::string>& include_hashes,
        const std::vector<std::string>& defines,
        const std::vector<std::string>& include_dirs);
    static std::string compute_filelist_key(
        const std::string& loom_version,
        const std::string& manifest_hash,
        const std::vector<std::string>& effective_hashes);

    // Maintenance
    Status prune();
    Status clear();
    Status vacuum();
    Result<CacheStats> get_stats();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace loom

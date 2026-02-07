#include <loom/build_cache.hpp>
#include <loom/sha256.hpp>
#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace loom {

// ---------------------------------------------------------------------------
// Binary serialization helpers (varint + length-prefixed strings)
// ---------------------------------------------------------------------------

static const char MAGIC[] = "LPR\x01";
static constexpr size_t MAGIC_LEN = 4;

namespace ser {

static void write_varint(std::vector<uint8_t>& buf, uint64_t val) {
    while (val >= 0x80) {
        buf.push_back(static_cast<uint8_t>(val & 0x7F) | 0x80);
        val >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(val));
}

static bool read_varint(const uint8_t*& p, const uint8_t* end, uint64_t& val) {
    val = 0;
    unsigned shift = 0;
    while (p < end) {
        uint8_t b = *p++;
        val |= static_cast<uint64_t>(b & 0x7F) << shift;
        if (!(b & 0x80)) return true;
        shift += 7;
        if (shift >= 64) return false;
    }
    return false;
}

static void write_string(std::vector<uint8_t>& buf, const std::string& s) {
    write_varint(buf, s.size());
    buf.insert(buf.end(), s.begin(), s.end());
}

static bool read_string(const uint8_t*& p, const uint8_t* end, std::string& s) {
    uint64_t len;
    if (!read_varint(p, end, len)) return false;
    if (p + len > end) return false;
    s.assign(reinterpret_cast<const char*>(p), static_cast<size_t>(len));
    p += len;
    return true;
}

static void write_bool(std::vector<uint8_t>& buf, bool v) {
    buf.push_back(v ? 1 : 0);
}

static bool read_bool(const uint8_t*& p, const uint8_t* end, bool& v) {
    if (p >= end) return false;
    v = (*p++ != 0);
    return true;
}

static void write_byte(std::vector<uint8_t>& buf, uint8_t v) {
    buf.push_back(v);
}

static bool read_byte(const uint8_t*& p, const uint8_t* end, uint8_t& v) {
    if (p >= end) return false;
    v = *p++;
    return true;
}

static void write_int(std::vector<uint8_t>& buf, int v) {
    write_varint(buf, static_cast<uint64_t>(static_cast<uint32_t>(v)));
}

static bool read_int(const uint8_t*& p, const uint8_t* end, int& v) {
    uint64_t raw;
    if (!read_varint(p, end, raw)) return false;
    v = static_cast<int>(static_cast<uint32_t>(raw));
    return true;
}

// SourcePos: skip file (redundant in cache context), serialize line + col
static void write_pos(std::vector<uint8_t>& buf, const SourcePos& pos) {
    write_int(buf, pos.line);
    write_int(buf, pos.col);
}

static bool read_pos(const uint8_t*& p, const uint8_t* end, SourcePos& pos) {
    return read_int(p, end, pos.line) && read_int(p, end, pos.col);
}

} // namespace ser

// ---------------------------------------------------------------------------
// Serialization of ParseResult
// ---------------------------------------------------------------------------

static std::vector<uint8_t> serialize_parse_result(const ParseResult& pr) {
    std::vector<uint8_t> buf;
    buf.reserve(1024);

    // Magic
    buf.insert(buf.end(), MAGIC, MAGIC + MAGIC_LEN);

    // Units
    ser::write_varint(buf, pr.units.size());
    for (auto& u : pr.units) {
        ser::write_byte(buf, static_cast<uint8_t>(u.kind));
        ser::write_string(buf, u.name);
        ser::write_int(buf, u.start_line);
        ser::write_int(buf, u.end_line);
        ser::write_int(buf, u.depth);
        ser::write_bool(buf, u.has_defparam);

        // Ports
        ser::write_varint(buf, u.ports.size());
        for (auto& p : u.ports) {
            ser::write_string(buf, p.name);
            ser::write_byte(buf, static_cast<uint8_t>(p.direction));
            ser::write_string(buf, p.type_text);
            ser::write_pos(buf, p.pos);
        }

        // Params
        ser::write_varint(buf, u.params.size());
        for (auto& p : u.params) {
            ser::write_string(buf, p.name);
            ser::write_string(buf, p.default_text);
            ser::write_bool(buf, p.is_localparam);
            ser::write_pos(buf, p.pos);
        }

        // Instantiations
        ser::write_varint(buf, u.instantiations.size());
        for (auto& i : u.instantiations) {
            ser::write_string(buf, i.module_name);
            ser::write_string(buf, i.instance_name);
            ser::write_bool(buf, i.is_parameterized);
            ser::write_pos(buf, i.pos);
        }

        // Imports
        ser::write_varint(buf, u.imports.size());
        for (auto& imp : u.imports) {
            ser::write_string(buf, imp.package_name);
            ser::write_string(buf, imp.symbol);
            ser::write_bool(buf, imp.is_wildcard);
            ser::write_pos(buf, imp.pos);
        }

        // Always blocks
        ser::write_varint(buf, u.always_blocks.size());
        for (auto& ab : u.always_blocks) {
            ser::write_byte(buf, static_cast<uint8_t>(ab.kind));
            ser::write_string(buf, ab.label);
            ser::write_varint(buf, ab.assignments.size());
            for (auto& a : ab.assignments) {
                ser::write_bool(buf, a.is_blocking);
                ser::write_string(buf, a.target);
                ser::write_pos(buf, a.pos);
            }
            ser::write_pos(buf, ab.pos);
        }

        // Case statements
        ser::write_varint(buf, u.case_statements.size());
        for (auto& cs : u.case_statements) {
            ser::write_byte(buf, static_cast<uint8_t>(cs.kind));
            ser::write_bool(buf, cs.has_default);
            ser::write_bool(buf, cs.is_unique);
            ser::write_bool(buf, cs.is_priority);
            ser::write_pos(buf, cs.pos);
        }

        // Signals
        ser::write_varint(buf, u.signals.size());
        for (auto& s : u.signals) {
            ser::write_string(buf, s.name);
            ser::write_string(buf, s.type_text);
            ser::write_pos(buf, s.pos);
        }

        // Generate blocks
        ser::write_varint(buf, u.generate_blocks.size());
        for (auto& g : u.generate_blocks) {
            ser::write_string(buf, g.label);
            ser::write_bool(buf, g.has_label);
            ser::write_pos(buf, g.pos);
        }

        // Labeled blocks
        ser::write_varint(buf, u.labeled_blocks.size());
        for (auto& lb : u.labeled_blocks) {
            ser::write_string(buf, lb.begin_label);
            ser::write_string(buf, lb.end_label);
            ser::write_bool(buf, lb.labels_match);
            ser::write_pos(buf, lb.pos);
        }

        // DesignUnit pos
        ser::write_pos(buf, u.pos);
    }

    // Diagnostics
    ser::write_varint(buf, pr.diagnostics.size());
    for (auto& d : pr.diagnostics) {
        ser::write_string(buf, d.message);
        ser::write_string(buf, d.file);
        ser::write_int(buf, d.line);
        ser::write_int(buf, d.col);
    }

    return buf;
}

static Result<ParseResult> deserialize_parse_result(const uint8_t* data, size_t len) {
    if (len < MAGIC_LEN || std::memcmp(data, MAGIC, MAGIC_LEN) != 0) {
        return LoomError(LoomError::Checksum, "Invalid cache magic bytes");
    }

    const uint8_t* p = data + MAGIC_LEN;
    const uint8_t* end = data + len;
    ParseResult pr;

    uint64_t num_units;
    if (!ser::read_varint(p, end, num_units))
        return LoomError(LoomError::IO, "Corrupted cache: truncated unit count");

    pr.units.resize(static_cast<size_t>(num_units));
    for (size_t ui = 0; ui < num_units; ++ui) {
        auto& u = pr.units[ui];
        uint8_t kind_byte;
        if (!ser::read_byte(p, end, kind_byte))
            return LoomError(LoomError::IO, "Corrupted cache: truncated unit kind");
        u.kind = static_cast<DesignUnitKind>(kind_byte);

        if (!ser::read_string(p, end, u.name) ||
            !ser::read_int(p, end, u.start_line) ||
            !ser::read_int(p, end, u.end_line) ||
            !ser::read_int(p, end, u.depth) ||
            !ser::read_bool(p, end, u.has_defparam))
            return LoomError(LoomError::IO, "Corrupted cache: truncated unit header");

        // Ports
        uint64_t n;
        if (!ser::read_varint(p, end, n))
            return LoomError(LoomError::IO, "Corrupted cache: truncated port count");
        u.ports.resize(static_cast<size_t>(n));
        for (auto& port : u.ports) {
            uint8_t dir;
            if (!ser::read_string(p, end, port.name) ||
                !ser::read_byte(p, end, dir) ||
                !ser::read_string(p, end, port.type_text) ||
                !ser::read_pos(p, end, port.pos))
                return LoomError(LoomError::IO, "Corrupted cache: truncated port");
            port.direction = static_cast<PortDirection>(dir);
        }

        // Params
        if (!ser::read_varint(p, end, n))
            return LoomError(LoomError::IO, "Corrupted cache: truncated param count");
        u.params.resize(static_cast<size_t>(n));
        for (auto& param : u.params) {
            if (!ser::read_string(p, end, param.name) ||
                !ser::read_string(p, end, param.default_text) ||
                !ser::read_bool(p, end, param.is_localparam) ||
                !ser::read_pos(p, end, param.pos))
                return LoomError(LoomError::IO, "Corrupted cache: truncated param");
        }

        // Instantiations
        if (!ser::read_varint(p, end, n))
            return LoomError(LoomError::IO, "Corrupted cache: truncated inst count");
        u.instantiations.resize(static_cast<size_t>(n));
        for (auto& inst : u.instantiations) {
            if (!ser::read_string(p, end, inst.module_name) ||
                !ser::read_string(p, end, inst.instance_name) ||
                !ser::read_bool(p, end, inst.is_parameterized) ||
                !ser::read_pos(p, end, inst.pos))
                return LoomError(LoomError::IO, "Corrupted cache: truncated instantiation");
        }

        // Imports
        if (!ser::read_varint(p, end, n))
            return LoomError(LoomError::IO, "Corrupted cache: truncated import count");
        u.imports.resize(static_cast<size_t>(n));
        for (auto& imp : u.imports) {
            if (!ser::read_string(p, end, imp.package_name) ||
                !ser::read_string(p, end, imp.symbol) ||
                !ser::read_bool(p, end, imp.is_wildcard) ||
                !ser::read_pos(p, end, imp.pos))
                return LoomError(LoomError::IO, "Corrupted cache: truncated import");
        }

        // Always blocks
        if (!ser::read_varint(p, end, n))
            return LoomError(LoomError::IO, "Corrupted cache: truncated always count");
        u.always_blocks.resize(static_cast<size_t>(n));
        for (auto& ab : u.always_blocks) {
            uint8_t ak;
            if (!ser::read_byte(p, end, ak) ||
                !ser::read_string(p, end, ab.label))
                return LoomError(LoomError::IO, "Corrupted cache: truncated always block");
            ab.kind = static_cast<AlwaysKind>(ak);

            uint64_t na;
            if (!ser::read_varint(p, end, na))
                return LoomError(LoomError::IO, "Corrupted cache: truncated assignment count");
            ab.assignments.resize(static_cast<size_t>(na));
            for (auto& a : ab.assignments) {
                if (!ser::read_bool(p, end, a.is_blocking) ||
                    !ser::read_string(p, end, a.target) ||
                    !ser::read_pos(p, end, a.pos))
                    return LoomError(LoomError::IO, "Corrupted cache: truncated assignment");
            }
            if (!ser::read_pos(p, end, ab.pos))
                return LoomError(LoomError::IO, "Corrupted cache: truncated always pos");
        }

        // Case statements
        if (!ser::read_varint(p, end, n))
            return LoomError(LoomError::IO, "Corrupted cache: truncated case count");
        u.case_statements.resize(static_cast<size_t>(n));
        for (auto& cs : u.case_statements) {
            uint8_t ck;
            if (!ser::read_byte(p, end, ck) ||
                !ser::read_bool(p, end, cs.has_default) ||
                !ser::read_bool(p, end, cs.is_unique) ||
                !ser::read_bool(p, end, cs.is_priority) ||
                !ser::read_pos(p, end, cs.pos))
                return LoomError(LoomError::IO, "Corrupted cache: truncated case statement");
            cs.kind = static_cast<CaseKind>(ck);
        }

        // Signals
        if (!ser::read_varint(p, end, n))
            return LoomError(LoomError::IO, "Corrupted cache: truncated signal count");
        u.signals.resize(static_cast<size_t>(n));
        for (auto& s : u.signals) {
            if (!ser::read_string(p, end, s.name) ||
                !ser::read_string(p, end, s.type_text) ||
                !ser::read_pos(p, end, s.pos))
                return LoomError(LoomError::IO, "Corrupted cache: truncated signal");
        }

        // Generate blocks
        if (!ser::read_varint(p, end, n))
            return LoomError(LoomError::IO, "Corrupted cache: truncated generate count");
        u.generate_blocks.resize(static_cast<size_t>(n));
        for (auto& g : u.generate_blocks) {
            if (!ser::read_string(p, end, g.label) ||
                !ser::read_bool(p, end, g.has_label) ||
                !ser::read_pos(p, end, g.pos))
                return LoomError(LoomError::IO, "Corrupted cache: truncated generate block");
        }

        // Labeled blocks
        if (!ser::read_varint(p, end, n))
            return LoomError(LoomError::IO, "Corrupted cache: truncated labeled count");
        u.labeled_blocks.resize(static_cast<size_t>(n));
        for (auto& lb : u.labeled_blocks) {
            if (!ser::read_string(p, end, lb.begin_label) ||
                !ser::read_string(p, end, lb.end_label) ||
                !ser::read_bool(p, end, lb.labels_match) ||
                !ser::read_pos(p, end, lb.pos))
                return LoomError(LoomError::IO, "Corrupted cache: truncated labeled block");
        }

        // DesignUnit pos
        if (!ser::read_pos(p, end, u.pos))
            return LoomError(LoomError::IO, "Corrupted cache: truncated unit pos");
    }

    // Diagnostics
    uint64_t num_diags;
    if (!ser::read_varint(p, end, num_diags))
        return LoomError(LoomError::IO, "Corrupted cache: truncated diag count");
    pr.diagnostics.resize(static_cast<size_t>(num_diags));
    for (auto& d : pr.diagnostics) {
        if (!ser::read_string(p, end, d.message) ||
            !ser::read_string(p, end, d.file) ||
            !ser::read_int(p, end, d.line) ||
            !ser::read_int(p, end, d.col))
            return LoomError(LoomError::IO, "Corrupted cache: truncated diagnostic");
    }

    return Result<ParseResult>::ok(std::move(pr));
}

// ---------------------------------------------------------------------------
// Comma-separated string helpers (for filelist storage)
// ---------------------------------------------------------------------------

static std::string join_strings(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0) out += ',';
        out += v[i];
    }
    return out;
}

static std::vector<std::string> split_strings(const std::string& s) {
    std::vector<std::string> out;
    if (s.empty()) return out;
    size_t start = 0;
    while (true) {
        size_t pos = s.find(',', start);
        if (pos == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

// ---------------------------------------------------------------------------
// pImpl
// ---------------------------------------------------------------------------

static const std::string SCHEMA_VERSION = "8";

struct BuildCache::Impl {
    sqlite3* db = nullptr;

    // Prepared statements (lazily initialized, cached)
    sqlite3_stmt* stmt_lookup_stat = nullptr;
    sqlite3_stmt* stmt_update_stat = nullptr;
    sqlite3_stmt* stmt_remove_stat = nullptr;
    sqlite3_stmt* stmt_lookup_parse = nullptr;
    sqlite3_stmt* stmt_store_parse = nullptr;
    sqlite3_stmt* stmt_get_includes = nullptr;
    sqlite3_stmt* stmt_del_includes = nullptr;
    sqlite3_stmt* stmt_insert_include = nullptr;
    sqlite3_stmt* stmt_find_includers = nullptr;
    sqlite3_stmt* stmt_get_edges = nullptr;
    sqlite3_stmt* stmt_del_edges = nullptr;
    sqlite3_stmt* stmt_insert_edge = nullptr;
    sqlite3_stmt* stmt_lookup_filelist = nullptr;
    sqlite3_stmt* stmt_store_filelist = nullptr;

    ~Impl() {
        finalize_all();
        if (db) sqlite3_close(db);
    }

    void finalize_all() {
        auto fin = [](sqlite3_stmt*& s) {
            if (s) { sqlite3_finalize(s); s = nullptr; }
        };
        fin(stmt_lookup_stat);
        fin(stmt_update_stat);
        fin(stmt_remove_stat);
        fin(stmt_lookup_parse);
        fin(stmt_store_parse);
        fin(stmt_get_includes);
        fin(stmt_del_includes);
        fin(stmt_insert_include);
        fin(stmt_find_includers);
        fin(stmt_get_edges);
        fin(stmt_del_edges);
        fin(stmt_insert_edge);
        fin(stmt_lookup_filelist);
        fin(stmt_store_filelist);
    }

    Status prepare(const char* sql, sqlite3_stmt*& out) {
        if (out) return ok_status();
        int rc = sqlite3_prepare_v2(db, sql, -1, &out, nullptr);
        if (rc != SQLITE_OK) {
            return LoomError(LoomError::IO,
                std::string("SQLite prepare failed: ") + sqlite3_errmsg(db));
        }
        return ok_status();
    }

    Status exec(const char* sql) {
        char* errmsg = nullptr;
        int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK) {
            std::string msg = errmsg ? errmsg : "unknown error";
            sqlite3_free(errmsg);
            return LoomError(LoomError::IO, "SQLite exec failed: " + msg);
        }
        return ok_status();
    }

    Status init_schema() {
        LOOM_TRY(exec(
            "CREATE TABLE IF NOT EXISTS schema_info ("
            "  key TEXT PRIMARY KEY,"
            "  value TEXT"
            ");"
            "CREATE TABLE IF NOT EXISTS file_stat ("
            "  path TEXT PRIMARY KEY,"
            "  inode INTEGER,"
            "  mtime_sec INTEGER,"
            "  mtime_nsec INTEGER,"
            "  size INTEGER,"
            "  content_hash TEXT"
            ");"
            "CREATE TABLE IF NOT EXISTS parse_result ("
            "  content_hash TEXT PRIMARY KEY,"
            "  serialized BLOB,"
            "  created_at INTEGER"
            ");"
            "CREATE TABLE IF NOT EXISTS include_dep ("
            "  source_hash TEXT,"
            "  include_path TEXT,"
            "  include_hash TEXT,"
            "  PRIMARY KEY (source_hash, include_path)"
            ");"
            "CREATE TABLE IF NOT EXISTS dep_edge ("
            "  source_hash TEXT,"
            "  source_unit TEXT,"
            "  target_unit TEXT,"
            "  PRIMARY KEY (source_hash, source_unit, target_unit)"
            ");"
            "CREATE TABLE IF NOT EXISTS filelist ("
            "  filelist_key TEXT PRIMARY KEY,"
            "  file_list TEXT,"
            "  top_modules TEXT,"
            "  created_at INTEGER"
            ");"
        ));

        // Check schema version
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db,
            "SELECT value FROM schema_info WHERE key='version'", -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW) {
                const char* ver = reinterpret_cast<const char*>(
                    sqlite3_column_text(stmt, 0));
                if (ver && std::string(ver) != SCHEMA_VERSION) {
                    sqlite3_finalize(stmt);
                    // Version mismatch — clear and re-init
                    LOOM_TRY(exec(
                        "DELETE FROM file_stat;"
                        "DELETE FROM parse_result;"
                        "DELETE FROM include_dep;"
                        "DELETE FROM dep_edge;"
                        "DELETE FROM filelist;"
                    ));
                    std::string ver_sql = "INSERT OR REPLACE INTO schema_info (key, value) "
                        "VALUES ('version', '" + SCHEMA_VERSION + "');";
                    LOOM_TRY(exec(ver_sql.c_str()));
                    return ok_status();
                }
                sqlite3_finalize(stmt);
            } else {
                sqlite3_finalize(stmt);
                // No version row yet
                std::string ver_sql = "INSERT OR REPLACE INTO schema_info (key, value) "
                    "VALUES ('version', '" + SCHEMA_VERSION + "');";
                LOOM_TRY(exec(ver_sql.c_str()));
            }
        } else {
            if (stmt) sqlite3_finalize(stmt);
        }

        return ok_status();
    }
};

// ---------------------------------------------------------------------------
// BuildCache public interface
// ---------------------------------------------------------------------------

BuildCache::BuildCache() : impl_(std::make_unique<Impl>()) {}
BuildCache::~BuildCache() = default;
BuildCache::BuildCache(BuildCache&&) noexcept = default;
BuildCache& BuildCache::operator=(BuildCache&&) noexcept = default;

std::string BuildCache::default_cache_path() {
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp";
    return std::string(home) + "/.loom/cache/loom_cache.db";
}

Status BuildCache::open(const std::string& db_path) {
    close();

    // Create parent directories
    fs::path parent = fs::path(db_path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec) {
            return LoomError(LoomError::IO,
                "Failed to create cache directory: " + parent.string());
        }
    }

    int rc = sqlite3_open(db_path.c_str(), &impl_->db);
    if (rc != SQLITE_OK) {
        // Corruption or invalid file — try to delete and retry
        std::string err_msg = impl_->db ? sqlite3_errmsg(impl_->db) : "unknown";
        if (impl_->db) { sqlite3_close(impl_->db); impl_->db = nullptr; }

        std::error_code ec;
        fs::remove(db_path, ec);
        rc = sqlite3_open(db_path.c_str(), &impl_->db);
        if (rc != SQLITE_OK) {
            if (impl_->db) { sqlite3_close(impl_->db); impl_->db = nullptr; }
            return LoomError(LoomError::IO,
                "Failed to open cache database: " + err_msg);
        }
    }

    // Set PRAGMAs and init schema; if anything fails, delete and retry
    auto setup = [&]() -> Status {
        LOOM_TRY(impl_->exec(
            "PRAGMA journal_mode=WAL;"
            "PRAGMA synchronous=NORMAL;"
            "PRAGMA cache_size=10000;"
        ));
        LOOM_TRY(impl_->init_schema());
        return ok_status();
    };

    auto setup_result = setup();
    if (setup_result.is_err()) {
        // Corrupt DB — delete and retry once
        close();
        std::error_code ec;
        fs::remove(db_path, ec);
        fs::remove(db_path + "-wal", ec);
        fs::remove(db_path + "-shm", ec);
        rc = sqlite3_open(db_path.c_str(), &impl_->db);
        if (rc != SQLITE_OK) {
            if (impl_->db) { sqlite3_close(impl_->db); impl_->db = nullptr; }
            return LoomError(LoomError::IO, "Failed to recreate cache database");
        }
        LOOM_TRY(setup());
    }

    return ok_status();
}

void BuildCache::close() {
    if (impl_->db) {
        impl_->finalize_all();
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }
}

bool BuildCache::is_open() const {
    return impl_->db != nullptr;
}

// ---------------------------------------------------------------------------
// Stat cache
// ---------------------------------------------------------------------------

Result<FileStatEntry> BuildCache::lookup_stat(const std::string& path) {
    LOOM_TRY(impl_->prepare(
        "SELECT path, inode, mtime_sec, mtime_nsec, size, content_hash "
        "FROM file_stat WHERE path=?",
        impl_->stmt_lookup_stat));

    sqlite3_reset(impl_->stmt_lookup_stat);
    sqlite3_bind_text(impl_->stmt_lookup_stat, 1, path.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(impl_->stmt_lookup_stat);
    if (rc == SQLITE_ROW) {
        FileStatEntry e;
        e.path = reinterpret_cast<const char*>(sqlite3_column_text(impl_->stmt_lookup_stat, 0));
        e.inode = static_cast<uint64_t>(sqlite3_column_int64(impl_->stmt_lookup_stat, 1));
        e.mtime_sec = sqlite3_column_int64(impl_->stmt_lookup_stat, 2);
        e.mtime_nsec = sqlite3_column_int64(impl_->stmt_lookup_stat, 3);
        e.size = sqlite3_column_int64(impl_->stmt_lookup_stat, 4);
        e.content_hash = reinterpret_cast<const char*>(sqlite3_column_text(impl_->stmt_lookup_stat, 5));
        return Result<FileStatEntry>::ok(std::move(e));
    }

    return LoomError(LoomError::NotFound, "No stat entry for: " + path);
}

Status BuildCache::update_stat(const FileStatEntry& entry) {
    LOOM_TRY(impl_->prepare(
        "INSERT OR REPLACE INTO file_stat (path, inode, mtime_sec, mtime_nsec, size, content_hash) "
        "VALUES (?, ?, ?, ?, ?, ?)",
        impl_->stmt_update_stat));

    sqlite3_reset(impl_->stmt_update_stat);
    sqlite3_bind_text(impl_->stmt_update_stat, 1, entry.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(impl_->stmt_update_stat, 2, static_cast<int64_t>(entry.inode));
    sqlite3_bind_int64(impl_->stmt_update_stat, 3, entry.mtime_sec);
    sqlite3_bind_int64(impl_->stmt_update_stat, 4, entry.mtime_nsec);
    sqlite3_bind_int64(impl_->stmt_update_stat, 5, entry.size);
    sqlite3_bind_text(impl_->stmt_update_stat, 6, entry.content_hash.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(impl_->stmt_update_stat);
    if (rc != SQLITE_DONE) {
        return LoomError(LoomError::IO,
            std::string("Failed to update stat: ") + sqlite3_errmsg(impl_->db));
    }
    return ok_status();
}

Status BuildCache::remove_stat(const std::string& path) {
    LOOM_TRY(impl_->prepare(
        "DELETE FROM file_stat WHERE path=?",
        impl_->stmt_remove_stat));

    sqlite3_reset(impl_->stmt_remove_stat);
    sqlite3_bind_text(impl_->stmt_remove_stat, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(impl_->stmt_remove_stat);
    return ok_status();
}

Result<std::string> BuildCache::cached_file_hash(const std::string& path) {
    // Resolve symlinks
    std::error_code ec;
    std::string canonical = fs::canonical(path, ec).string();
    if (ec) {
        return LoomError(LoomError::IO, "Cannot resolve path: " + path);
    }

    // Check stat cache
    struct stat st;
    if (::stat(canonical.c_str(), &st) != 0) {
        return LoomError(LoomError::IO, "Cannot stat file: " + canonical);
    }

    auto cached = lookup_stat(canonical);
    if (cached.is_ok()) {
        auto& e = cached.value();
        if (e.inode == static_cast<uint64_t>(st.st_ino) &&
            e.mtime_sec == static_cast<int64_t>(st.st_mtim.tv_sec) &&
            e.mtime_nsec == static_cast<int64_t>(st.st_mtim.tv_nsec) &&
            e.size == static_cast<int64_t>(st.st_size)) {
            return Result<std::string>::ok(e.content_hash);
        }
    }

    // Cache miss or stat changed — compute hash
    std::string hash = SHA256::hash_file(canonical);

    FileStatEntry entry;
    entry.path = canonical;
    entry.inode = static_cast<uint64_t>(st.st_ino);
    entry.mtime_sec = static_cast<int64_t>(st.st_mtim.tv_sec);
    entry.mtime_nsec = static_cast<int64_t>(st.st_mtim.tv_nsec);
    entry.size = static_cast<int64_t>(st.st_size);
    entry.content_hash = hash;
    LOOM_TRY(update_stat(entry));

    return Result<std::string>::ok(std::move(hash));
}

// ---------------------------------------------------------------------------
// Parse cache
// ---------------------------------------------------------------------------

Result<ParseResult> BuildCache::lookup_parse(const std::string& content_hash) {
    LOOM_TRY(impl_->prepare(
        "SELECT serialized FROM parse_result WHERE content_hash=?",
        impl_->stmt_lookup_parse));

    sqlite3_reset(impl_->stmt_lookup_parse);
    sqlite3_bind_text(impl_->stmt_lookup_parse, 1, content_hash.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(impl_->stmt_lookup_parse);
    if (rc == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(impl_->stmt_lookup_parse, 0);
        int blob_len = sqlite3_column_bytes(impl_->stmt_lookup_parse, 0);
        if (!blob || blob_len <= 0) {
            return LoomError(LoomError::IO, "Empty serialized data for: " + content_hash);
        }
        return deserialize_parse_result(
            static_cast<const uint8_t*>(blob), static_cast<size_t>(blob_len));
    }

    return LoomError(LoomError::NotFound, "No parse cache for: " + content_hash);
}

Status BuildCache::store_parse(const std::string& content_hash, const ParseResult& result) {
    LOOM_TRY(impl_->prepare(
        "INSERT OR REPLACE INTO parse_result (content_hash, serialized, created_at) "
        "VALUES (?, ?, ?)",
        impl_->stmt_store_parse));

    auto blob = serialize_parse_result(result);
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto epoch_sec = std::chrono::duration_cast<std::chrono::seconds>(now).count();

    sqlite3_reset(impl_->stmt_store_parse);
    sqlite3_bind_text(impl_->stmt_store_parse, 1, content_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(impl_->stmt_store_parse, 2, blob.data(),
                      static_cast<int>(blob.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(impl_->stmt_store_parse, 3, epoch_sec);

    int rc = sqlite3_step(impl_->stmt_store_parse);
    if (rc != SQLITE_DONE) {
        return LoomError(LoomError::IO,
            std::string("Failed to store parse result: ") + sqlite3_errmsg(impl_->db));
    }
    return ok_status();
}

// ---------------------------------------------------------------------------
// Include dependency tracking
// ---------------------------------------------------------------------------

Result<std::vector<IncludeDepEntry>> BuildCache::get_includes(const std::string& source_hash) {
    LOOM_TRY(impl_->prepare(
        "SELECT source_hash, include_path, include_hash "
        "FROM include_dep WHERE source_hash=?",
        impl_->stmt_get_includes));

    sqlite3_reset(impl_->stmt_get_includes);
    sqlite3_bind_text(impl_->stmt_get_includes, 1, source_hash.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<IncludeDepEntry> out;
    while (sqlite3_step(impl_->stmt_get_includes) == SQLITE_ROW) {
        IncludeDepEntry e;
        e.source_hash = reinterpret_cast<const char*>(sqlite3_column_text(impl_->stmt_get_includes, 0));
        e.include_path = reinterpret_cast<const char*>(sqlite3_column_text(impl_->stmt_get_includes, 1));
        e.include_hash = reinterpret_cast<const char*>(sqlite3_column_text(impl_->stmt_get_includes, 2));
        out.push_back(std::move(e));
    }
    return Result<std::vector<IncludeDepEntry>>::ok(std::move(out));
}

Status BuildCache::store_includes(const std::string& source_hash,
                                  const std::vector<IncludeDepEntry>& deps) {
    LOOM_TRY(impl_->prepare(
        "DELETE FROM include_dep WHERE source_hash=?",
        impl_->stmt_del_includes));
    LOOM_TRY(impl_->prepare(
        "INSERT INTO include_dep (source_hash, include_path, include_hash) VALUES (?, ?, ?)",
        impl_->stmt_insert_include));

    sqlite3_reset(impl_->stmt_del_includes);
    sqlite3_bind_text(impl_->stmt_del_includes, 1, source_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(impl_->stmt_del_includes);

    for (auto& d : deps) {
        sqlite3_reset(impl_->stmt_insert_include);
        sqlite3_bind_text(impl_->stmt_insert_include, 1, d.source_hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(impl_->stmt_insert_include, 2, d.include_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(impl_->stmt_insert_include, 3, d.include_hash.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(impl_->stmt_insert_include);
        if (rc != SQLITE_DONE) {
            return LoomError(LoomError::IO,
                std::string("Failed to insert include dep: ") + sqlite3_errmsg(impl_->db));
        }
    }
    return ok_status();
}

Result<std::vector<std::string>> BuildCache::find_includers(const std::string& include_hash) {
    LOOM_TRY(impl_->prepare(
        "SELECT DISTINCT source_hash FROM include_dep WHERE include_hash=?",
        impl_->stmt_find_includers));

    sqlite3_reset(impl_->stmt_find_includers);
    sqlite3_bind_text(impl_->stmt_find_includers, 1, include_hash.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<std::string> out;
    while (sqlite3_step(impl_->stmt_find_includers) == SQLITE_ROW) {
        out.emplace_back(reinterpret_cast<const char*>(
            sqlite3_column_text(impl_->stmt_find_includers, 0)));
    }
    return Result<std::vector<std::string>>::ok(std::move(out));
}

// ---------------------------------------------------------------------------
// Dependency edge tracking
// ---------------------------------------------------------------------------

Result<std::vector<DepEdgeEntry>> BuildCache::get_edges(const std::string& source_hash) {
    LOOM_TRY(impl_->prepare(
        "SELECT source_hash, source_unit, target_unit "
        "FROM dep_edge WHERE source_hash=?",
        impl_->stmt_get_edges));

    sqlite3_reset(impl_->stmt_get_edges);
    sqlite3_bind_text(impl_->stmt_get_edges, 1, source_hash.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<DepEdgeEntry> out;
    while (sqlite3_step(impl_->stmt_get_edges) == SQLITE_ROW) {
        DepEdgeEntry e;
        e.source_hash = reinterpret_cast<const char*>(sqlite3_column_text(impl_->stmt_get_edges, 0));
        e.source_unit = reinterpret_cast<const char*>(sqlite3_column_text(impl_->stmt_get_edges, 1));
        e.target_unit = reinterpret_cast<const char*>(sqlite3_column_text(impl_->stmt_get_edges, 2));
        out.push_back(std::move(e));
    }
    return Result<std::vector<DepEdgeEntry>>::ok(std::move(out));
}

Status BuildCache::store_edges(const std::string& source_hash,
                               const std::vector<DepEdgeEntry>& edges) {
    LOOM_TRY(impl_->prepare(
        "DELETE FROM dep_edge WHERE source_hash=?",
        impl_->stmt_del_edges));
    LOOM_TRY(impl_->prepare(
        "INSERT INTO dep_edge (source_hash, source_unit, target_unit) VALUES (?, ?, ?)",
        impl_->stmt_insert_edge));

    sqlite3_reset(impl_->stmt_del_edges);
    sqlite3_bind_text(impl_->stmt_del_edges, 1, source_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(impl_->stmt_del_edges);

    for (auto& e : edges) {
        sqlite3_reset(impl_->stmt_insert_edge);
        sqlite3_bind_text(impl_->stmt_insert_edge, 1, e.source_hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(impl_->stmt_insert_edge, 2, e.source_unit.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(impl_->stmt_insert_edge, 3, e.target_unit.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(impl_->stmt_insert_edge);
        if (rc != SQLITE_DONE) {
            return LoomError(LoomError::IO,
                std::string("Failed to insert dep edge: ") + sqlite3_errmsg(impl_->db));
        }
    }
    return ok_status();
}

// ---------------------------------------------------------------------------
// Filelist cache
// ---------------------------------------------------------------------------

Result<FilelistCacheEntry> BuildCache::lookup_filelist(const std::string& filelist_key) {
    LOOM_TRY(impl_->prepare(
        "SELECT filelist_key, file_list, top_modules, created_at "
        "FROM filelist WHERE filelist_key=?",
        impl_->stmt_lookup_filelist));

    sqlite3_reset(impl_->stmt_lookup_filelist);
    sqlite3_bind_text(impl_->stmt_lookup_filelist, 1, filelist_key.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(impl_->stmt_lookup_filelist);
    if (rc == SQLITE_ROW) {
        FilelistCacheEntry e;
        e.filelist_key = reinterpret_cast<const char*>(sqlite3_column_text(impl_->stmt_lookup_filelist, 0));
        e.file_list = split_strings(
            reinterpret_cast<const char*>(sqlite3_column_text(impl_->stmt_lookup_filelist, 1)));
        e.top_modules = split_strings(
            reinterpret_cast<const char*>(sqlite3_column_text(impl_->stmt_lookup_filelist, 2)));
        e.created_at = sqlite3_column_int64(impl_->stmt_lookup_filelist, 3);
        return Result<FilelistCacheEntry>::ok(std::move(e));
    }

    return LoomError(LoomError::NotFound, "No filelist cache for key: " + filelist_key);
}

Status BuildCache::store_filelist(const FilelistCacheEntry& entry) {
    LOOM_TRY(impl_->prepare(
        "INSERT OR REPLACE INTO filelist (filelist_key, file_list, top_modules, created_at) "
        "VALUES (?, ?, ?, ?)",
        impl_->stmt_store_filelist));

    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto epoch_sec = std::chrono::duration_cast<std::chrono::seconds>(now).count();

    sqlite3_reset(impl_->stmt_store_filelist);
    sqlite3_bind_text(impl_->stmt_store_filelist, 1, entry.filelist_key.c_str(), -1, SQLITE_TRANSIENT);

    std::string fl = join_strings(entry.file_list);
    std::string tm = join_strings(entry.top_modules);
    sqlite3_bind_text(impl_->stmt_store_filelist, 2, fl.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(impl_->stmt_store_filelist, 3, tm.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(impl_->stmt_store_filelist, 4, epoch_sec);

    int rc = sqlite3_step(impl_->stmt_store_filelist);
    if (rc != SQLITE_DONE) {
        return LoomError(LoomError::IO,
            std::string("Failed to store filelist: ") + sqlite3_errmsg(impl_->db));
    }
    return ok_status();
}

// ---------------------------------------------------------------------------
// Hash computation helpers
// ---------------------------------------------------------------------------

std::string BuildCache::compute_effective_hash(
    const std::string& content_hash,
    const std::vector<std::string>& include_hashes,
    const std::vector<std::string>& defines,
    const std::vector<std::string>& include_dirs)
{
    // Sort all inputs for determinism
    auto sorted_includes = include_hashes;
    auto sorted_defines = defines;
    auto sorted_dirs = include_dirs;
    std::sort(sorted_includes.begin(), sorted_includes.end());
    std::sort(sorted_defines.begin(), sorted_defines.end());
    std::sort(sorted_dirs.begin(), sorted_dirs.end());

    std::string combined = content_hash;
    for (auto& h : sorted_includes) combined += "|" + h;
    combined += "||";
    for (auto& d : sorted_defines) combined += "|" + d;
    combined += "||";
    for (auto& d : sorted_dirs) combined += "|" + d;

    return SHA256::hash_hex(combined);
}

std::string BuildCache::compute_filelist_key(
    const std::string& loom_version,
    const std::string& manifest_hash,
    const std::vector<std::string>& effective_hashes)
{
    auto sorted = effective_hashes;
    std::sort(sorted.begin(), sorted.end());

    std::string combined = loom_version + "|" + manifest_hash;
    for (auto& h : sorted) combined += "|" + h;

    return SHA256::hash_hex(combined);
}

// ---------------------------------------------------------------------------
// Maintenance
// ---------------------------------------------------------------------------

Status BuildCache::prune() {
    LOOM_TRY(impl_->exec(
        "DELETE FROM parse_result WHERE content_hash NOT IN "
        "(SELECT content_hash FROM file_stat);"
    ));
    LOOM_TRY(impl_->exec(
        "DELETE FROM include_dep WHERE source_hash NOT IN "
        "(SELECT content_hash FROM file_stat);"
    ));
    LOOM_TRY(impl_->exec(
        "DELETE FROM dep_edge WHERE source_hash NOT IN "
        "(SELECT content_hash FROM file_stat);"
    ));
    return ok_status();
}

Status BuildCache::clear() {
    LOOM_TRY(impl_->exec(
        "DELETE FROM file_stat;"
        "DELETE FROM parse_result;"
        "DELETE FROM include_dep;"
        "DELETE FROM dep_edge;"
        "DELETE FROM filelist;"
    ));
    return ok_status();
}

Status BuildCache::vacuum() {
    return impl_->exec("VACUUM;");
}

Result<CacheStats> BuildCache::get_stats() {
    CacheStats stats;

    auto count_table = [&](const char* table, int64_t& out) -> Status {
        std::string sql = std::string("SELECT COUNT(*) FROM ") + table;
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return LoomError(LoomError::IO,
                std::string("Failed to count ") + table);
        }
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            out = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return ok_status();
    };

    LOOM_TRY(count_table("file_stat", stats.file_stat_count));
    LOOM_TRY(count_table("parse_result", stats.parse_result_count));
    LOOM_TRY(count_table("include_dep", stats.include_dep_count));
    LOOM_TRY(count_table("dep_edge", stats.dep_edge_count));
    LOOM_TRY(count_table("filelist", stats.filelist_count));

    // Total size
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db,
        "SELECT page_count * page_size FROM pragma_page_count(), pragma_page_size()",
        -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats.total_bytes = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    } else {
        if (stmt) sqlite3_finalize(stmt);
    }

    return Result<CacheStats>::ok(std::move(stats));
}

} // namespace loom

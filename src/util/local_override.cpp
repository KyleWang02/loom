#include <loom/local_override.hpp>
#include <loom/log.hpp>
#include <tomlplusplus/toml.hpp>
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace loom {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// LocalOverrides::parse
// ---------------------------------------------------------------------------

Result<LocalOverrides> LocalOverrides::parse(const std::string& toml_str) {
    toml::table doc;
    try {
        doc = toml::parse(toml_str);
    } catch (const toml::parse_error& e) {
        return LoomError{LoomError::Parse,
            std::string("Loom.local parse error: ") + e.what()};
    }

    LocalOverrides lo;

    auto* overrides_tbl = doc["overrides"].as_table();
    if (!overrides_tbl) {
        // No [overrides] section — return empty
        return Result<LocalOverrides>::ok(std::move(lo));
    }

    for (const auto& [key, val] : *overrides_tbl) {
        std::string name(key);

        if (!val.is_table()) {
            return LoomError{LoomError::Parse,
                "override '" + name + "' must be a table"};
        }
        const auto& tbl = *val.as_table();

        auto path_val = tbl["path"].value<std::string>();
        auto git_val = tbl["git"].value<std::string>();

        if (path_val && git_val) {
            return LoomError{LoomError::Parse,
                "override '" + name + "' cannot have both 'path' and 'git'"};
        }

        if (!path_val && !git_val) {
            return LoomError{LoomError::Parse,
                "override '" + name + "' must have either 'path' or 'git'"};
        }

        OverrideSource src;
        if (path_val) {
            src.kind = OverrideSource::Kind::Path;
            src.path = std::string(*path_val);
        } else {
            src.kind = OverrideSource::Kind::Git;
            src.url = std::string(*git_val);
            if (auto v = tbl["branch"].value<std::string>()) src.branch = std::string(*v);
            if (auto v = tbl["tag"].value<std::string>()) src.tag = std::string(*v);
            if (auto v = tbl["rev"].value<std::string>()) src.rev = std::string(*v);
        }

        lo.overrides[name] = std::move(src);
    }

    return Result<LocalOverrides>::ok(std::move(lo));
}

// ---------------------------------------------------------------------------
// LocalOverrides::load
// ---------------------------------------------------------------------------

Result<LocalOverrides> LocalOverrides::load(const fs::path& local_file) {
    std::ifstream file(local_file);
    if (!file.is_open()) {
        return LoomError{LoomError::IO,
            "cannot open local overrides file: " + local_file.string()};
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return LocalOverrides::parse(ss.str());
}

// ---------------------------------------------------------------------------
// Query methods
// ---------------------------------------------------------------------------

bool LocalOverrides::has_override(const std::string& name) const {
    return overrides.count(name) > 0;
}

const OverrideSource* LocalOverrides::get_override(const std::string& name) const {
    auto it = overrides.find(name);
    if (it == overrides.end()) return nullptr;
    return &it->second;
}

size_t LocalOverrides::count() const {
    return overrides.size();
}

bool LocalOverrides::empty() const {
    return overrides.empty();
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

Status LocalOverrides::validate() const {
    for (const auto& [name, src] : overrides) {
        if (src.kind == OverrideSource::Kind::Path) {
            std::error_code ec;
            if (!fs::is_directory(src.path, ec)) {
                return LoomError{LoomError::IO,
                    "override '" + name + "': path does not exist or is not a directory: " + src.path};
            }
            if (!fs::exists(fs::path(src.path) / "Loom.toml", ec)) {
                return LoomError{LoomError::Manifest,
                    "override '" + name + "': path '" + src.path + "' does not contain a Loom.toml"};
            }
        } else {
            // Git override
            if (src.url.empty()) {
                return LoomError{LoomError::Parse,
                    "override '" + name + "': git URL cannot be empty"};
            }
        }
    }
    return ok_status();
}

// ---------------------------------------------------------------------------
// warn_active
// ---------------------------------------------------------------------------

void LocalOverrides::warn_active() const {
    for (const auto& [name, src] : overrides) {
        if (src.kind == OverrideSource::Kind::Path) {
            log::warn("local override active: %s -> path '%s'",
                name.c_str(), src.path.c_str());
        } else {
            std::string ref;
            if (!src.branch.empty()) ref = "branch=" + src.branch;
            else if (!src.tag.empty()) ref = "tag=" + src.tag;
            else if (!src.rev.empty()) ref = "rev=" + src.rev;

            if (ref.empty()) {
                log::warn("local override active: %s -> git '%s'",
                    name.c_str(), src.url.c_str());
            } else {
                log::warn("local override active: %s -> git '%s' (%s)",
                    name.c_str(), src.url.c_str(), ref.c_str());
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Discovery and suppression
// ---------------------------------------------------------------------------

Result<LocalOverrides> discover_local_overrides(const fs::path& project_root) {
    fs::path local_file = project_root / "Loom.local";
    std::error_code ec;
    if (!fs::exists(local_file, ec)) {
        // No Loom.local — return empty overrides (not an error)
        return Result<LocalOverrides>::ok(LocalOverrides{});
    }
    return LocalOverrides::load(local_file);
}

bool should_suppress_overrides(bool no_local_flag) {
    if (no_local_flag) return true;
    const char* env = std::getenv("LOOM_NO_LOCAL");
    if (env && std::string(env) == "1") return true;
    return false;
}

} // namespace loom

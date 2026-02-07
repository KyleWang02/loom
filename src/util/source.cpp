#include <loom/source.hpp>

namespace loom {

Result<std::monostate> Dependency::validate() const {
    int source_count = 0;
    if (git.has_value()) ++source_count;
    if (path.has_value()) ++source_count;
    if (workspace) ++source_count;
    if (member) ++source_count;

    if (source_count == 0) {
        return LoomError{LoomError::Dependency,
            "dependency '" + name + "' has no source",
            "specify one of: git, path, workspace = true, or member = true"};
    }

    if (source_count > 1) {
        return LoomError{LoomError::Dependency,
            "dependency '" + name + "' has multiple sources",
            "git, path, workspace, and member are mutually exclusive"};
    }

    // Git source validation: exactly one of tag/version/rev/branch
    if (git.has_value()) {
        const auto& g = git.value();
        if (g.url.empty()) {
            return LoomError{LoomError::Dependency,
                "dependency '" + name + "' has empty git URL"};
        }

        int ref_count = 0;
        if (g.tag.has_value()) ++ref_count;
        if (g.version.has_value()) ++ref_count;
        if (g.rev.has_value()) ++ref_count;
        if (g.branch.has_value()) ++ref_count;

        if (ref_count == 0) {
            return LoomError{LoomError::Dependency,
                "dependency '" + name + "' git source has no ref",
                "specify one of: tag, version, rev, or branch"};
        }

        if (ref_count > 1) {
            return LoomError{LoomError::Dependency,
                "dependency '" + name + "' git source has multiple refs",
                "tag, version, rev, and branch are mutually exclusive"};
        }

        // Validate version constraint if present
        if (g.version.has_value()) {
            auto vr = VersionReq::parse(g.version.value());
            if (vr.is_err()) {
                return LoomError{LoomError::Dependency,
                    "dependency '" + name + "' has invalid version constraint: " +
                    vr.error().message};
            }
        }
    }

    // Path source validation
    if (path.has_value()) {
        if (path.value().path.empty()) {
            return LoomError{LoomError::Dependency,
                "dependency '" + name + "' has empty path"};
        }
    }

    return ok_status();
}

} // namespace loom

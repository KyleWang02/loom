#pragma once

#include <loom/result.hpp>
#include <string>
#include <vector>

namespace loom {

// Full version: major.minor.micro[-label]
struct Version {
    int major = 0;
    int minor = 0;
    int micro = 0;
    std::string label;  // e.g., "alpha", "rc1", empty for release

    static Result<Version> parse(const std::string& s);
    std::string to_string() const;

    bool operator==(const Version& o) const;
    bool operator!=(const Version& o) const;
    bool operator<(const Version& o) const;
    bool operator<=(const Version& o) const;
    bool operator>(const Version& o) const;
    bool operator>=(const Version& o) const;
};

// Partial version for constraints: "1", "1.2", "1.2.3"
struct PartialVersion {
    int major = 0;
    int minor = -1;  // -1 means unset
    int micro = -1;  // -1 means unset

    static Result<PartialVersion> parse(const std::string& s);
    std::string to_string() const;
};

// Semver constraint operators
enum class ConstraintOp {
    Exact,       // =1.2.3
    Caret,       // ^1.2.3 (compatible with)
    Tilde,       // ~1.2.3 (patch-level changes)
    GreaterEq,   // >=1.2.3
    Greater,     // >1.2.3
    LessEq,      // <=1.2.3
    Less,        // <1.2.3
};

struct VersionConstraint {
    ConstraintOp op;
    PartialVersion version;

    bool matches(const Version& v) const;
    std::string to_string() const;
};

// Compound constraint: ">=1.0.0, <2.0.0"
struct VersionReq {
    std::vector<VersionConstraint> constraints;

    static Result<VersionReq> parse(const std::string& s);
    bool matches(const Version& v) const;
    std::string to_string() const;
};

} // namespace loom

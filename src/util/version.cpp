#include <loom/version.hpp>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace loom {

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------

Result<Version> Version::parse(const std::string& s) {
    if (s.empty()) {
        return LoomError{LoomError::Version, "empty version string"};
    }

    Version v;
    size_t pos = 0;

    // Parse major
    size_t dot1 = s.find('.', pos);
    if (dot1 == std::string::npos) {
        return LoomError{LoomError::Version,
            "invalid version '" + s + "'",
            "expected format: major.minor.micro[-label]"};
    }
    try {
        v.major = std::stoi(s.substr(pos, dot1 - pos));
    } catch (...) {
        return LoomError{LoomError::Version,
            "invalid major version in '" + s + "'"};
    }

    // Parse minor
    pos = dot1 + 1;
    size_t dot2 = s.find('.', pos);
    if (dot2 == std::string::npos) {
        return LoomError{LoomError::Version,
            "invalid version '" + s + "'",
            "expected format: major.minor.micro[-label]"};
    }
    try {
        // Minor may be followed by '-' for label or end
        v.minor = std::stoi(s.substr(pos, dot2 - pos));
    } catch (...) {
        return LoomError{LoomError::Version,
            "invalid minor version in '" + s + "'"};
    }

    // Parse micro (and optional label)
    pos = dot2 + 1;
    size_t dash = s.find('-', pos);
    if (dash != std::string::npos) {
        try {
            v.micro = std::stoi(s.substr(pos, dash - pos));
        } catch (...) {
            return LoomError{LoomError::Version,
                "invalid micro version in '" + s + "'"};
        }
        v.label = s.substr(dash + 1);
        if (v.label.empty()) {
            return LoomError{LoomError::Version,
                "empty label after '-' in '" + s + "'"};
        }
    } else {
        try {
            v.micro = std::stoi(s.substr(pos));
        } catch (...) {
            return LoomError{LoomError::Version,
                "invalid micro version in '" + s + "'"};
        }
    }

    if (v.major < 0 || v.minor < 0 || v.micro < 0) {
        return LoomError{LoomError::Version,
            "negative version component in '" + s + "'"};
    }

    return Result<Version>::ok(std::move(v));
}

std::string Version::to_string() const {
    std::string s = std::to_string(major) + "." +
                    std::to_string(minor) + "." +
                    std::to_string(micro);
    if (!label.empty()) {
        s += "-" + label;
    }
    return s;
}

bool Version::operator==(const Version& o) const {
    return major == o.major && minor == o.minor &&
           micro == o.micro && label == o.label;
}

bool Version::operator!=(const Version& o) const { return !(*this == o); }

bool Version::operator<(const Version& o) const {
    if (major != o.major) return major < o.major;
    if (minor != o.minor) return minor < o.minor;
    if (micro != o.micro) return micro < o.micro;
    // Pre-release (non-empty label) < release (empty label)
    if (label.empty() && !o.label.empty()) return false;
    if (!label.empty() && o.label.empty()) return true;
    return label < o.label;
}

bool Version::operator<=(const Version& o) const { return !(o < *this); }
bool Version::operator>(const Version& o) const { return o < *this; }
bool Version::operator>=(const Version& o) const { return !(*this < o); }

// ---------------------------------------------------------------------------
// PartialVersion
// ---------------------------------------------------------------------------

Result<PartialVersion> PartialVersion::parse(const std::string& s) {
    if (s.empty()) {
        return LoomError{LoomError::Version, "empty partial version string"};
    }

    PartialVersion pv;
    size_t dot1 = s.find('.');
    if (dot1 == std::string::npos) {
        // Just major: "1"
        try {
            pv.major = std::stoi(s);
        } catch (...) {
            return LoomError{LoomError::Version,
                "invalid partial version '" + s + "'"};
        }
        return Result<PartialVersion>::ok(pv);
    }

    // major.minor or major.minor.micro
    try {
        pv.major = std::stoi(s.substr(0, dot1));
    } catch (...) {
        return LoomError{LoomError::Version,
            "invalid major in partial version '" + s + "'"};
    }

    size_t dot2 = s.find('.', dot1 + 1);
    if (dot2 == std::string::npos) {
        // major.minor: "1.2"
        try {
            pv.minor = std::stoi(s.substr(dot1 + 1));
        } catch (...) {
            return LoomError{LoomError::Version,
                "invalid minor in partial version '" + s + "'"};
        }
        return Result<PartialVersion>::ok(pv);
    }

    // major.minor.micro: "1.2.3"
    try {
        pv.minor = std::stoi(s.substr(dot1 + 1, dot2 - dot1 - 1));
        pv.micro = std::stoi(s.substr(dot2 + 1));
    } catch (...) {
        return LoomError{LoomError::Version,
            "invalid partial version '" + s + "'"};
    }

    return Result<PartialVersion>::ok(pv);
}

std::string PartialVersion::to_string() const {
    std::string s = std::to_string(major);
    if (minor >= 0) {
        s += "." + std::to_string(minor);
        if (micro >= 0) {
            s += "." + std::to_string(micro);
        }
    }
    return s;
}

// ---------------------------------------------------------------------------
// VersionConstraint
// ---------------------------------------------------------------------------

bool VersionConstraint::matches(const Version& v) const {
    // Expand partial version to full for comparison
    int req_major = version.major;
    int req_minor = version.minor >= 0 ? version.minor : 0;
    int req_micro = version.micro >= 0 ? version.micro : 0;

    Version req;
    req.major = req_major;
    req.minor = req_minor;
    req.micro = req_micro;

    switch (op) {
    case ConstraintOp::Exact:
        return v.major == req_major && v.minor == req_minor &&
               v.micro == req_micro && v.label.empty();

    case ConstraintOp::Caret:
        // ^X.Y.Z: >=X.Y.Z and <next major breaking change
        // ^0.0.Z: only exact match
        // ^0.Y.Z (Y>0): >=0.Y.Z, <0.(Y+1).0
        // ^X.Y.Z (X>0): >=X.Y.Z, <(X+1).0.0
        if (!v.label.empty()) return false;
        if (v < req) return false;
        if (req_major > 0) {
            return v.major == req_major;
        }
        if (req_minor > 0) {
            return v.major == 0 && v.minor == req_minor;
        }
        // ^0.0.Z
        return v.major == 0 && v.minor == 0 && v.micro == req_micro;

    case ConstraintOp::Tilde:
        // ~X.Y.Z: >=X.Y.Z, <X.(Y+1).0
        if (!v.label.empty()) return false;
        if (v < req) return false;
        return v.major == req_major && v.minor == req_minor;

    case ConstraintOp::GreaterEq:
        if (!v.label.empty()) return false;
        return v >= req;

    case ConstraintOp::Greater:
        if (!v.label.empty()) return false;
        return v > req;

    case ConstraintOp::LessEq:
        if (!v.label.empty()) return false;
        return v <= req;

    case ConstraintOp::Less:
        if (!v.label.empty()) return false;
        return v < req;
    }
    return false;
}

std::string VersionConstraint::to_string() const {
    std::string prefix;
    switch (op) {
    case ConstraintOp::Exact:     prefix = "="; break;
    case ConstraintOp::Caret:     prefix = "^"; break;
    case ConstraintOp::Tilde:     prefix = "~"; break;
    case ConstraintOp::GreaterEq: prefix = ">="; break;
    case ConstraintOp::Greater:   prefix = ">"; break;
    case ConstraintOp::LessEq:    prefix = "<="; break;
    case ConstraintOp::Less:      prefix = "<"; break;
    }
    return prefix + version.to_string();
}

// ---------------------------------------------------------------------------
// VersionReq
// ---------------------------------------------------------------------------

static Result<VersionConstraint> parse_single_constraint(const std::string& s) {
    size_t pos = 0;
    // Skip whitespace
    while (pos < s.size() && s[pos] == ' ') ++pos;

    ConstraintOp op = ConstraintOp::Caret; // default
    if (pos < s.size()) {
        if (s[pos] == '^') {
            op = ConstraintOp::Caret;
            ++pos;
        } else if (s[pos] == '~') {
            op = ConstraintOp::Tilde;
            ++pos;
        } else if (s[pos] == '=' ) {
            op = ConstraintOp::Exact;
            ++pos;
        } else if (pos + 1 < s.size() && s[pos] == '>' && s[pos + 1] == '=') {
            op = ConstraintOp::GreaterEq;
            pos += 2;
        } else if (s[pos] == '>') {
            op = ConstraintOp::Greater;
            ++pos;
        } else if (pos + 1 < s.size() && s[pos] == '<' && s[pos + 1] == '=') {
            op = ConstraintOp::LessEq;
            pos += 2;
        } else if (s[pos] == '<') {
            op = ConstraintOp::Less;
            ++pos;
        }
        // No prefix = caret (default, like Cargo)
    }

    // Skip whitespace after operator
    while (pos < s.size() && s[pos] == ' ') ++pos;

    std::string ver_str = s.substr(pos);
    // Trim trailing whitespace
    while (!ver_str.empty() && ver_str.back() == ' ') ver_str.pop_back();

    if (ver_str.empty()) {
        return LoomError{LoomError::Version,
            "missing version in constraint '" + s + "'"};
    }

    auto pv = PartialVersion::parse(ver_str);
    if (pv.is_err()) return std::move(pv).error();

    VersionConstraint vc;
    vc.op = op;
    vc.version = pv.value();
    return Result<VersionConstraint>::ok(vc);
}

Result<VersionReq> VersionReq::parse(const std::string& s) {
    if (s.empty()) {
        return LoomError{LoomError::Version, "empty version requirement"};
    }

    VersionReq req;
    std::istringstream stream(s);
    std::string token;

    while (std::getline(stream, token, ',')) {
        auto c = parse_single_constraint(token);
        if (c.is_err()) return std::move(c).error();
        req.constraints.push_back(std::move(c).value());
    }

    if (req.constraints.empty()) {
        return LoomError{LoomError::Version,
            "no constraints in version requirement '" + s + "'"};
    }

    return Result<VersionReq>::ok(std::move(req));
}

bool VersionReq::matches(const Version& v) const {
    return std::all_of(constraints.begin(), constraints.end(),
        [&](const VersionConstraint& c) { return c.matches(v); });
}

std::string VersionReq::to_string() const {
    std::string s;
    for (size_t i = 0; i < constraints.size(); ++i) {
        if (i > 0) s += ", ";
        s += constraints[i].to_string();
    }
    return s;
}

} // namespace loom

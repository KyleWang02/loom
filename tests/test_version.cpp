#include <catch2/catch.hpp>
#include <loom/version.hpp>

using namespace loom;

// ===== Version parsing =====

TEST_CASE("parse simple version", "[version]") {
    auto r = Version::parse("1.2.3");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().major == 1);
    REQUIRE(r.value().minor == 2);
    REQUIRE(r.value().micro == 3);
    REQUIRE(r.value().label.empty());
}

TEST_CASE("parse version with label", "[version]") {
    auto r = Version::parse("1.0.0-alpha");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().major == 1);
    REQUIRE(r.value().label == "alpha");
}

TEST_CASE("parse version with rc label", "[version]") {
    auto r = Version::parse("2.1.0-rc1");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().to_string() == "2.1.0-rc1");
}

TEST_CASE("version roundtrip", "[version]") {
    auto r = Version::parse("3.14.159");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().to_string() == "3.14.159");
}

TEST_CASE("version parse errors", "[version]") {
    REQUIRE(Version::parse("").is_err());
    REQUIRE(Version::parse("1").is_err());
    REQUIRE(Version::parse("1.2").is_err());
    REQUIRE(Version::parse("abc").is_err());
    REQUIRE(Version::parse("1.2.3-").is_err());
}

// ===== Version comparison =====

TEST_CASE("version equality", "[version]") {
    auto a = Version::parse("1.2.3").value();
    auto b = Version::parse("1.2.3").value();
    REQUIRE(a == b);
}

TEST_CASE("version ordering", "[version]") {
    auto v100 = Version::parse("1.0.0").value();
    auto v110 = Version::parse("1.1.0").value();
    auto v111 = Version::parse("1.1.1").value();
    auto v200 = Version::parse("2.0.0").value();

    REQUIRE(v100 < v110);
    REQUIRE(v110 < v111);
    REQUIRE(v111 < v200);
    REQUIRE(v200 > v100);
}

TEST_CASE("prerelease less than release", "[version]") {
    auto alpha = Version::parse("1.0.0-alpha").value();
    auto release = Version::parse("1.0.0").value();
    REQUIRE(alpha < release);
}

TEST_CASE("prerelease ordering", "[version]") {
    auto alpha = Version::parse("1.0.0-alpha").value();
    auto beta = Version::parse("1.0.0-beta").value();
    REQUIRE(alpha < beta);
}

// ===== PartialVersion =====

TEST_CASE("parse partial major only", "[version]") {
    auto r = PartialVersion::parse("1");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().major == 1);
    REQUIRE(r.value().minor == -1);
    REQUIRE(r.value().micro == -1);
    REQUIRE(r.value().to_string() == "1");
}

TEST_CASE("parse partial major.minor", "[version]") {
    auto r = PartialVersion::parse("1.2");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().major == 1);
    REQUIRE(r.value().minor == 2);
    REQUIRE(r.value().micro == -1);
    REQUIRE(r.value().to_string() == "1.2");
}

TEST_CASE("parse partial full", "[version]") {
    auto r = PartialVersion::parse("1.2.3");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().to_string() == "1.2.3");
}

// ===== Caret constraints =====

TEST_CASE("caret constraint ^1.2.3", "[version]") {
    auto req = VersionReq::parse("^1.2.3");
    REQUIRE(req.is_ok());
    REQUIRE(req.value().matches(Version::parse("1.2.3").value()));
    REQUIRE(req.value().matches(Version::parse("1.2.4").value()));
    REQUIRE(req.value().matches(Version::parse("1.9.0").value()));
    REQUIRE_FALSE(req.value().matches(Version::parse("2.0.0").value()));
    REQUIRE_FALSE(req.value().matches(Version::parse("1.2.2").value()));
}

TEST_CASE("caret constraint ^0.2.3", "[version]") {
    auto req = VersionReq::parse("^0.2.3");
    REQUIRE(req.is_ok());
    REQUIRE(req.value().matches(Version::parse("0.2.3").value()));
    REQUIRE(req.value().matches(Version::parse("0.2.9").value()));
    REQUIRE_FALSE(req.value().matches(Version::parse("0.3.0").value()));
    REQUIRE_FALSE(req.value().matches(Version::parse("1.0.0").value()));
}

TEST_CASE("caret constraint ^0.0.3", "[version]") {
    auto req = VersionReq::parse("^0.0.3");
    REQUIRE(req.is_ok());
    REQUIRE(req.value().matches(Version::parse("0.0.3").value()));
    REQUIRE_FALSE(req.value().matches(Version::parse("0.0.4").value()));
}

// ===== Tilde constraints =====

TEST_CASE("tilde constraint ~1.2.3", "[version]") {
    auto req = VersionReq::parse("~1.2.3");
    REQUIRE(req.is_ok());
    REQUIRE(req.value().matches(Version::parse("1.2.3").value()));
    REQUIRE(req.value().matches(Version::parse("1.2.9").value()));
    REQUIRE_FALSE(req.value().matches(Version::parse("1.3.0").value()));
}

// ===== Comparison constraints =====

TEST_CASE("greater-equal constraint", "[version]") {
    auto req = VersionReq::parse(">=1.0.0");
    REQUIRE(req.is_ok());
    REQUIRE(req.value().matches(Version::parse("1.0.0").value()));
    REQUIRE(req.value().matches(Version::parse("2.5.0").value()));
    REQUIRE_FALSE(req.value().matches(Version::parse("0.9.9").value()));
}

TEST_CASE("less-than constraint", "[version]") {
    auto req = VersionReq::parse("<2.0.0");
    REQUIRE(req.is_ok());
    REQUIRE(req.value().matches(Version::parse("1.9.9").value()));
    REQUIRE_FALSE(req.value().matches(Version::parse("2.0.0").value()));
}

// ===== Compound constraints =====

TEST_CASE("range constraint >=1.0.0, <2.0.0", "[version]") {
    auto req = VersionReq::parse(">=1.0.0, <2.0.0");
    REQUIRE(req.is_ok());
    REQUIRE(req.value().matches(Version::parse("1.0.0").value()));
    REQUIRE(req.value().matches(Version::parse("1.5.3").value()));
    REQUIRE_FALSE(req.value().matches(Version::parse("0.9.0").value()));
    REQUIRE_FALSE(req.value().matches(Version::parse("2.0.0").value()));
}

// ===== Bare version (no prefix) defaults to caret =====

TEST_CASE("bare version defaults to caret", "[version]") {
    auto req = VersionReq::parse("1.2.3");
    REQUIRE(req.is_ok());
    // Same as ^1.2.3
    REQUIRE(req.value().matches(Version::parse("1.2.3").value()));
    REQUIRE(req.value().matches(Version::parse("1.9.0").value()));
    REQUIRE_FALSE(req.value().matches(Version::parse("2.0.0").value()));
}

// ===== Constraint errors =====

TEST_CASE("empty version requirement error", "[version]") {
    REQUIRE(VersionReq::parse("").is_err());
}

// ===== Prerelease excluded from constraints =====

TEST_CASE("constraints exclude prerelease", "[version]") {
    auto req = VersionReq::parse("^1.0.0");
    REQUIRE(req.is_ok());
    REQUIRE_FALSE(req.value().matches(Version::parse("1.0.0-alpha").value()));
}

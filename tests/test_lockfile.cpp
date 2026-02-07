#include <catch2/catch.hpp>
#include <loom/lockfile.hpp>
#include <loom/source.hpp>

#include <cstdio>
#include <fstream>
#include <filesystem>
#include <sstream>

using namespace loom;

namespace fs = std::filesystem;

static std::string temp_dir() {
    const char* src = std::getenv("LOOM_SOURCE_DIR");
    std::string base = src ? std::string(src) : "..";
    std::string dir = base + "/build/test_lockfile_tmp";
    fs::create_directories(dir);
    return dir;
}

static std::string write_temp_file(const std::string& name,
                                    const std::string& content) {
    std::string path = temp_dir() + "/" + name;
    std::ofstream out(path);
    out << content;
    return path;
}

// ===== LockFile::load =====

TEST_CASE("load valid lockfile", "[lockfile]") {
    std::string toml = R"TOML(
loom_version = "0.1.0"

[root]
name = "my-soc"
version = "1.0.0"

[[packages]]
name = "uart_ip"
version = "1.3.0"
source = "git+https://github.com/org/uart.git"
commit = "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2"
ref = "v1.3.0"
checksum = "abc123"
dependencies = ["common_cells"]

[[packages]]
name = "common_cells"
version = "0.5.0"
source = "git+https://github.com/org/common.git"
commit = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeef"
ref = "v0.5.0"
checksum = "def456"
)TOML";

    auto path = write_temp_file("valid.lock", toml);
    auto r = LockFile::load(path);
    REQUIRE(r.is_ok());

    auto& lf = r.value();
    REQUIRE(lf.loom_version == "0.1.0");
    REQUIRE(lf.root_name == "my-soc");
    REQUIRE(lf.root_version == "1.0.0");
    REQUIRE(lf.packages.size() == 2);

    REQUIRE(lf.packages[0].name == "uart_ip");
    REQUIRE(lf.packages[0].version == "1.3.0");
    REQUIRE(lf.packages[0].source == "git+https://github.com/org/uart.git");
    REQUIRE(lf.packages[0].commit == "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2");
    REQUIRE(lf.packages[0].ref == "v1.3.0");
    REQUIRE(lf.packages[0].dependencies.size() == 1);
    REQUIRE(lf.packages[0].dependencies[0] == "common_cells");

    REQUIRE(lf.packages[1].name == "common_cells");
    REQUIRE(lf.packages[1].dependencies.empty());
}

TEST_CASE("load nonexistent lockfile", "[lockfile]") {
    auto r = LockFile::load("/nonexistent/path/Loom.lock");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::NotFound);
}

TEST_CASE("load empty lockfile", "[lockfile]") {
    auto path = write_temp_file("empty.lock", "loom_version = \"0.1.0\"\n");
    auto r = LockFile::load(path);
    REQUIRE(r.is_ok());
    REQUIRE(r.value().packages.empty());
    REQUIRE(r.value().loom_version == "0.1.0");
}

// ===== LockFile::save =====

TEST_CASE("save lockfile produces valid TOML", "[lockfile]") {
    LockFile lf;
    lf.loom_version = "0.1.0";
    lf.root_name = "test-project";
    lf.root_version = "2.0.0";

    LockedPackage p1;
    p1.name = "b_pkg";
    p1.version = "1.0.0";
    p1.source = "git+https://example.com/b.git";
    p1.commit = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    p1.ref = "v1.0.0";
    p1.checksum = "bbb_checksum";

    LockedPackage p2;
    p2.name = "a_pkg";
    p2.version = "2.0.0";
    p2.source = "git+https://example.com/a.git";
    p2.commit = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    p2.ref = "v2.0.0";
    p2.checksum = "aaa_checksum";
    p2.dependencies = {"b_pkg"};

    lf.packages = {p1, p2};

    auto path = temp_dir() + "/saved.lock";
    auto status = lf.save(path);
    REQUIRE(status.is_ok());

    // Verify file was written
    std::ifstream in(path);
    REQUIRE(in.good());
    std::stringstream buf;
    buf << in.rdbuf();
    std::string content = buf.str();

    // Should contain header comment
    REQUIRE(content.find("auto-generated") != std::string::npos);

    // Packages should be sorted: a_pkg before b_pkg
    auto a_pos = content.find("a_pkg");
    auto b_pos = content.find("b_pkg");
    REQUIRE(a_pos < b_pos);
}

// ===== Roundtrip =====

TEST_CASE("lockfile roundtrip", "[lockfile]") {
    LockFile lf;
    lf.loom_version = "0.1.0";
    lf.root_name = "roundtrip";
    lf.root_version = "1.0.0";

    LockedPackage p;
    p.name = "test_ip";
    p.version = "3.2.1";
    p.source = "git+https://github.com/org/test.git";
    p.commit = "1234567890abcdef1234567890abcdef12345678";
    p.ref = "v3.2.1";
    p.checksum = "sha256_checksum_here";
    p.dependencies = {"dep_a", "dep_b"};
    lf.packages = {p};

    auto path = temp_dir() + "/roundtrip.lock";
    REQUIRE(lf.save(path).is_ok());

    auto r = LockFile::load(path);
    REQUIRE(r.is_ok());

    auto& lf2 = r.value();
    REQUIRE(lf2.loom_version == lf.loom_version);
    REQUIRE(lf2.root_name == lf.root_name);
    REQUIRE(lf2.root_version == lf.root_version);
    REQUIRE(lf2.packages.size() == 1);

    auto& p2 = lf2.packages[0];
    REQUIRE(p2.name == p.name);
    REQUIRE(p2.version == p.version);
    REQUIRE(p2.source == p.source);
    REQUIRE(p2.commit == p.commit);
    REQUIRE(p2.ref == p.ref);
    REQUIRE(p2.checksum == p.checksum);
    REQUIRE(p2.dependencies == p.dependencies);
}

// ===== LockFile::find =====

TEST_CASE("find locked package hit", "[lockfile]") {
    LockFile lf;
    LockedPackage p;
    p.name = "uart_ip";
    p.version = "1.0.0";
    lf.packages = {p};

    auto* found = lf.find("uart_ip");
    REQUIRE(found != nullptr);
    REQUIRE(found->version == "1.0.0");
}

TEST_CASE("find locked package miss", "[lockfile]") {
    LockFile lf;
    LockedPackage p;
    p.name = "uart_ip";
    lf.packages = {p};

    REQUIRE(lf.find("nonexistent") == nullptr);
}

// ===== LockFile::is_stale =====

TEST_CASE("is_stale detects new dependency", "[lockfile]") {
    LockFile lf;
    LockedPackage p;
    p.name = "existing";
    p.source = "git+https://example.com/existing.git";
    lf.packages = {p};

    Dependency d1;
    d1.name = "existing";
    d1.git = GitSource{"https://example.com/existing.git", "v1.0.0", {}, {}, {}};

    Dependency d2;
    d2.name = "new_dep";
    d2.git = GitSource{"https://example.com/new.git", "v1.0.0", {}, {}, {}};

    REQUIRE(lf.is_stale({d1, d2}));
}

TEST_CASE("is_stale detects removed dependency", "[lockfile]") {
    LockFile lf;
    LockedPackage p1;
    p1.name = "kept";
    p1.source = "git+https://example.com/kept.git";

    LockedPackage p2;
    p2.name = "removed";
    p2.source = "git+https://example.com/removed.git";

    lf.packages = {p1, p2};

    Dependency d;
    d.name = "kept";
    d.git = GitSource{"https://example.com/kept.git", "v1.0.0", {}, {}, {}};

    REQUIRE(lf.is_stale({d}));
}

TEST_CASE("is_stale detects changed source", "[lockfile]") {
    LockFile lf;
    LockedPackage p;
    p.name = "dep";
    p.source = "git+https://old-url.com/dep.git";
    lf.packages = {p};

    Dependency d;
    d.name = "dep";
    d.git = GitSource{"https://new-url.com/dep.git", "v1.0.0", {}, {}, {}};

    REQUIRE(lf.is_stale({d}));
}

TEST_CASE("is_stale returns false when matching", "[lockfile]") {
    LockFile lf;
    LockedPackage p;
    p.name = "uart_ip";
    p.source = "git+https://github.com/org/uart.git";
    lf.packages = {p};

    Dependency d;
    d.name = "uart_ip";
    d.git = GitSource{"https://github.com/org/uart.git", "v1.0.0", {}, {}, {}};

    REQUIRE_FALSE(lf.is_stale({d}));
}

TEST_CASE("is_stale with path dependency", "[lockfile]") {
    LockFile lf;
    LockedPackage p;
    p.name = "local_ip";
    p.source = "path+../local";
    lf.packages = {p};

    Dependency d;
    d.name = "local_ip";
    d.path = PathSource{"../local"};

    REQUIRE_FALSE(lf.is_stale({d}));
}

TEST_CASE("is_stale empty lockfile with deps", "[lockfile]") {
    LockFile lf;

    Dependency d;
    d.name = "new";
    d.git = GitSource{"https://example.com/new.git", "v1.0.0", {}, {}, {}};

    REQUIRE(lf.is_stale({d}));
}

TEST_CASE("is_stale empty lockfile empty deps", "[lockfile]") {
    LockFile lf;
    REQUIRE_FALSE(lf.is_stale({}));
}

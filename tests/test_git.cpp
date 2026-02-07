#include <catch2/catch.hpp>
#include <loom/git.hpp>
#include <loom/version.hpp>

using namespace loom;

// ===== run_command() =====

TEST_CASE("run_command echo", "[git]") {
    auto r = run_command({"echo", "hello"});
    REQUIRE(r.is_ok());
    REQUIRE(r.value().exit_code == 0);
    REQUIRE(r.value().stdout_str == "hello\n");
}

TEST_CASE("run_command false returns nonzero", "[git]") {
    auto r = run_command({"false"});
    REQUIRE(r.is_ok());
    REQUIRE(r.value().exit_code != 0);
}

TEST_CASE("run_command captures stderr", "[git]") {
    auto r = run_command({"sh", "-c", "echo err >&2"});
    REQUIRE(r.is_ok());
    REQUIRE(r.value().stderr_str.find("err") != std::string::npos);
}

TEST_CASE("run_command empty args error", "[git]") {
    auto r = run_command({});
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::InvalidArg);
}

TEST_CASE("run_command with working dir", "[git]") {
    auto r = run_command({"pwd"}, "/tmp");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().exit_code == 0);
    // Output should contain /tmp (may have trailing newline)
    REQUIRE(r.value().stdout_str.find("/tmp") != std::string::npos);
}

TEST_CASE("run_command nonexistent binary", "[git]") {
    auto r = run_command({"__loom_nonexistent_binary_xyz__"});
    REQUIRE(r.is_ok());
    REQUIRE(r.value().exit_code == 127);
}

// ===== parse_ls_remote_tags() =====

TEST_CASE("parse empty ls-remote output", "[git]") {
    auto r = parse_ls_remote_tags("");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().empty());
}

TEST_CASE("parse ls-remote lightweight tags", "[git]") {
    std::string output =
        "aaa1111111111111111111111111111111111111\trefs/tags/v1.0.0\n"
        "bbb2222222222222222222222222222222222222\trefs/tags/v1.1.0\n"
        "ccc3333333333333333333333333333333333333\trefs/tags/v2.0.0\n";

    auto r = parse_ls_remote_tags(output);
    REQUIRE(r.is_ok());
    REQUIRE(r.value().size() == 3);
    // Sorted descending by version
    REQUIRE(r.value()[0].version == Version::parse("2.0.0").value());
    REQUIRE(r.value()[1].version == Version::parse("1.1.0").value());
    REQUIRE(r.value()[2].version == Version::parse("1.0.0").value());
}

TEST_CASE("parse ls-remote annotated tag deref", "[git]") {
    // Annotated tags produce two lines: the tag object and the deref ^{}
    std::string output =
        "tag_object_sha_not_commit_sha_xxxxxxxx\trefs/tags/v1.0.0\n"
        "actual_commit_sha_yyyyyyyyyyyyyyyyyyyy\trefs/tags/v1.0.0^{}\n";

    auto r = parse_ls_remote_tags(output);
    REQUIRE(r.is_ok());
    REQUIRE(r.value().size() == 1);
    // The deref SHA should be used
    REQUIRE(r.value()[0].commit == "actual_commit_sha_yyyyyyyyyyyyyyyyyyyy");
    REQUIRE(r.value()[0].name == "v1.0.0");
}

TEST_CASE("parse ls-remote skips non-semver tags", "[git]") {
    std::string output =
        "aaa1111111111111111111111111111111111111\trefs/tags/v1.0.0\n"
        "bbb2222222222222222222222222222222222222\trefs/tags/release-candidate\n"
        "ccc3333333333333333333333333333333333333\trefs/tags/nightly\n";

    auto r = parse_ls_remote_tags(output);
    REQUIRE(r.is_ok());
    REQUIRE(r.value().size() == 1);
    REQUIRE(r.value()[0].name == "v1.0.0");
}

TEST_CASE("parse ls-remote tags without v prefix", "[git]") {
    std::string output =
        "aaa1111111111111111111111111111111111111\trefs/tags/1.0.0\n"
        "bbb2222222222222222222222222222222222222\trefs/tags/2.3.4\n";

    auto r = parse_ls_remote_tags(output);
    REQUIRE(r.is_ok());
    REQUIRE(r.value().size() == 2);
}

// ===== resolve_version_from_tags() =====

TEST_CASE("resolve version caret constraint", "[git]") {
    std::string output =
        "aaa\trefs/tags/v1.0.0\n"
        "bbb\trefs/tags/v1.1.0\n"
        "ccc\trefs/tags/v1.2.0\n"
        "ddd\trefs/tags/v2.0.0\n";

    auto tags = parse_ls_remote_tags(output);
    REQUIRE(tags.is_ok());

    auto req = VersionReq::parse("^1.0.0");
    REQUIRE(req.is_ok());

    auto r = resolve_version_from_tags(tags.value(), req.value());
    REQUIRE(r.is_ok());
    // Should pick highest matching: 1.2.0
    REQUIRE(r.value().version == Version::parse("1.2.0").value());
}

TEST_CASE("resolve version tilde constraint", "[git]") {
    std::string output =
        "aaa\trefs/tags/v1.2.0\n"
        "bbb\trefs/tags/v1.2.5\n"
        "ccc\trefs/tags/v1.3.0\n";

    auto tags = parse_ls_remote_tags(output);
    REQUIRE(tags.is_ok());

    auto req = VersionReq::parse("~1.2.0");
    REQUIRE(req.is_ok());

    auto r = resolve_version_from_tags(tags.value(), req.value());
    REQUIRE(r.is_ok());
    // ~1.2.0 allows 1.2.x but not 1.3.0
    REQUIRE(r.value().version == Version::parse("1.2.5").value());
}

TEST_CASE("resolve version no match", "[git]") {
    std::string output =
        "aaa\trefs/tags/v1.0.0\n"
        "bbb\trefs/tags/v1.1.0\n";

    auto tags = parse_ls_remote_tags(output);
    REQUIRE(tags.is_ok());

    auto req = VersionReq::parse(">=2.0.0");
    REQUIRE(req.is_ok());

    auto r = resolve_version_from_tags(tags.value(), req.value());
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Version);
}

TEST_CASE("resolve version empty tags", "[git]") {
    std::vector<RemoteTag> empty;
    auto req = VersionReq::parse("^1.0.0");
    REQUIRE(req.is_ok());

    auto r = resolve_version_from_tags(empty, req.value());
    REQUIRE(r.is_err());
}

// ===== GitCli =====

TEST_CASE("git check_version", "[git]") {
    GitCli git;
    auto r = git.check_version();
    // This test only passes if git is installed
    if (r.is_ok()) {
        REQUIRE(!r.value().empty());
    }
}

TEST_CASE("git offline mode blocks network ops", "[git]") {
    GitCli git;
    git.set_offline(true);
    REQUIRE(git.is_offline());

    auto r1 = git.ls_remote("https://example.com/repo.git");
    REQUIRE(r1.is_err());
    REQUIRE(r1.error().code == LoomError::Network);

    auto r2 = git.clone_bare("https://example.com/repo.git", "/tmp/test");
    REQUIRE(r2.is_err());
    REQUIRE(r2.error().code == LoomError::Network);

    auto r3 = git.fetch("/tmp/nonexistent");
    REQUIRE(r3.is_err());
    REQUIRE(r3.error().code == LoomError::Network);
}

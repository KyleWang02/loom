#include <catch2/catch.hpp>
#include <loom/sha256.hpp>
#include <fstream>
#include <sstream>
#include <cstdlib>

using namespace loom;

// Resolve fixture paths relative to the source tree.
// CMake sets LOOM_SOURCE_DIR; fall back to a relative guess.
static std::string fixture_path(const std::string& name) {
    const char* src = std::getenv("LOOM_SOURCE_DIR");
    if (src) return std::string(src) + "/tests/fixtures/" + name;
    return "../tests/fixtures/" + name;
}

TEST_CASE("SHA256 empty string", "[sha256]") {
    auto hex = SHA256::hash_hex("");
    REQUIRE(hex == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("SHA256 'abc' (NIST vector)", "[sha256]") {
    auto hex = SHA256::hash_hex("abc");
    REQUIRE(hex == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("SHA256 448-bit message (NIST vector)", "[sha256]") {
    auto hex = SHA256::hash_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    REQUIRE(hex == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("SHA256 896-bit message (NIST vector)", "[sha256]") {
    auto hex = SHA256::hash_hex(
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
        "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu");
    REQUIRE(hex == "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST_CASE("SHA256 incremental update matches one-shot", "[sha256]") {
    // Feed "abc" one byte at a time
    SHA256 ctx;
    ctx.update(reinterpret_cast<const uint8_t*>("a"), 1);
    ctx.update(reinterpret_cast<const uint8_t*>("b"), 1);
    ctx.update(reinterpret_cast<const uint8_t*>("c"), 1);
    auto hex = SHA256::bytes_to_hex(ctx.finalize());
    REQUIRE(hex == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("SHA256 hash_hex one-shot convenience", "[sha256]") {
    // Same as NIST "abc" but verifying the static helper specifically
    auto hex = SHA256::hash_hex("abc");
    REQUIRE(hex == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("SHA256 hash_file matches hash_hex of same content", "[sha256]") {
    // Read the fixture file contents manually
    auto fpath = fixture_path("simple_module.v");
    std::ifstream in(fpath);
    REQUIRE(in.is_open());
    std::ostringstream buf;
    buf << in.rdbuf();
    std::string contents = buf.str();

    auto hex_from_string = SHA256::hash_hex(contents);
    auto hex_from_file = SHA256::hash_file(fpath);

    REQUIRE(!hex_from_string.empty());
    REQUIRE(hex_from_file == hex_from_string);
}

TEST_CASE("SHA256 hash_file nonexistent returns empty", "[sha256]") {
    auto hex = SHA256::hash_file("does_not_exist.v");
    REQUIRE(hex.empty());
}

TEST_CASE("SHA256 bytes_to_hex", "[sha256]") {
    std::array<uint8_t, 32> bytes = {
        0x00, 0x01, 0x02, 0x0a, 0x0f, 0x10, 0x7f, 0x80,
        0xff, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78,
        0x9a, 0xbc, 0xde, 0xf0, 0x11, 0x22, 0x33, 0x44,
        0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc
    };
    auto hex = SHA256::bytes_to_hex(bytes);
    REQUIRE(hex == "0001020a0f107f80ffabcdef123456789abcdef0112233445566778899aabbcc");
}

TEST_CASE("SHA256 large input (10000 bytes of 'a')", "[sha256]") {
    std::string input(10000, 'a');
    auto hex = SHA256::hash_hex(input);
    REQUIRE(hex == "27dd1f61b867b6a0f6e9d8a41c43231de52107e53ae424de8f847b821db4b711");
}

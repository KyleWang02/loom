#include <catch2/catch.hpp>
#include <loom/uuid.hpp>
#include <set>

using namespace loom;

TEST_CASE("UUID v4 version bits", "[uuid]") {
    auto u = Uuid::v4();
    // bytes[6] high nibble must be 0x4 (version 4)
    REQUIRE((u.bytes[6] & 0xF0) == 0x40);
}

TEST_CASE("UUID v4 variant bits", "[uuid]") {
    auto u = Uuid::v4();
    // bytes[8] top two bits must be 10 (variant 1)
    REQUIRE((u.bytes[8] & 0xC0) == 0x80);
}

TEST_CASE("UUID v4 generates unique values", "[uuid]") {
    std::set<std::string> seen;
    for (int i = 0; i < 100; ++i) {
        auto s = Uuid::v4().to_string();
        REQUIRE(seen.find(s) == seen.end());
        seen.insert(s);
    }
}

TEST_CASE("UUID to_string format", "[uuid]") {
    auto u = Uuid::v4();
    auto s = u.to_string();
    REQUIRE(s.size() == 36);
    REQUIRE(s[8] == '-');
    REQUIRE(s[13] == '-');
    REQUIRE(s[18] == '-');
    REQUIRE(s[23] == '-');
}

TEST_CASE("UUID to_string version character", "[uuid]") {
    auto u = Uuid::v4();
    auto s = u.to_string();
    // Position 14 is the version nibble: must be '4'
    REQUIRE(s[14] == '4');
}

TEST_CASE("UUID from_string roundtrip", "[uuid]") {
    auto u = Uuid::v4();
    auto s = u.to_string();
    auto parsed = Uuid::from_string(s);
    REQUIRE(parsed.is_ok());
    REQUIRE(parsed.value() == u);
}

TEST_CASE("UUID from_string rejects wrong length", "[uuid]") {
    auto r = Uuid::from_string("too-short");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Parse);
}

TEST_CASE("UUID from_string rejects missing dashes", "[uuid]") {
    // Correct length (36 chars) but no dashes
    auto r = Uuid::from_string("550e8400e29b41d4a716446655440000abcd");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Parse);
}

TEST_CASE("UUID from_string rejects invalid hex", "[uuid]") {
    // Valid format but contains 'g' which is not hex
    auto r = Uuid::from_string("550e8400-e29b-41d4-a716-44665544gggg");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Parse);
}

TEST_CASE("UUID from_string accepts uppercase", "[uuid]") {
    auto u = Uuid::v4();
    auto s = u.to_string();
    // Convert to uppercase
    std::string upper;
    for (char c : s) {
        if (c >= 'a' && c <= 'f') upper += (c - 'a' + 'A');
        else upper += c;
    }
    auto parsed = Uuid::from_string(upper);
    REQUIRE(parsed.is_ok());
    REQUIRE(parsed.value() == u);
}

TEST_CASE("UUID encode_base36 always 25 chars", "[uuid]") {
    for (int i = 0; i < 20; ++i) {
        auto encoded = Uuid::v4().encode_base36();
        REQUIRE(encoded.size() == 25);
    }
}

TEST_CASE("UUID base36 roundtrip random", "[uuid]") {
    auto u = Uuid::v4();
    auto encoded = u.encode_base36();
    auto decoded = Uuid::decode_base36(encoded);
    REQUIRE(decoded.is_ok());
    REQUIRE(decoded.value() == u);
}

TEST_CASE("UUID base36 roundtrip all-zero", "[uuid]") {
    Uuid u;
    u.bytes.fill(0x00);
    auto encoded = u.encode_base36();
    REQUIRE(encoded.size() == 25);
    REQUIRE(encoded == "0000000000000000000000000");
    auto decoded = Uuid::decode_base36(encoded);
    REQUIRE(decoded.is_ok());
    REQUIRE(decoded.value() == u);
}

TEST_CASE("UUID base36 roundtrip all-0xFF", "[uuid]") {
    Uuid u;
    u.bytes.fill(0xFF);
    auto encoded = u.encode_base36();
    REQUIRE(encoded.size() == 25);
    auto decoded = Uuid::decode_base36(encoded);
    REQUIRE(decoded.is_ok());
    REQUIRE(decoded.value() == u);
}

TEST_CASE("UUID decode_base36 rejects invalid chars", "[uuid]") {
    auto r = Uuid::decode_base36("0000000000000000000000!@#");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Parse);
}

TEST_CASE("UUID decode_base36 rejects wrong length", "[uuid]") {
    auto r = Uuid::decode_base36("abc");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Parse);
}

TEST_CASE("UUID equality operators", "[uuid]") {
    auto a = Uuid::v4();
    auto b = a; // copy
    REQUIRE(a == b);
    REQUIRE_FALSE(a != b);

    auto c = Uuid::v4();
    REQUIRE(a != c);
    REQUIRE_FALSE(a == c);
}

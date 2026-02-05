#include <catch2/catch.hpp>
#include <loom/result.hpp>
#include <memory>
#include <string>

using namespace loom;

// Helper function that uses LOOM_TRY
static Result<int> try_double(Result<int> input) {
    LOOM_TRY(input);
    return Result<int>::ok(input.value() * 2);
}

static Result<int> try_chain(bool fail_first) {
    auto first = fail_first
        ? Result<int>::err(LoomError{LoomError::Parse, "first failed"})
        : Result<int>::ok(10);
    LOOM_TRY(first);
    auto second = Result<int>::ok(first.value() + 5);
    LOOM_TRY(second);
    return Result<int>::ok(second.value());
}

TEST_CASE("Create Ok result and access value", "[result]") {
    auto r = Result<int>::ok(42);
    REQUIRE(r.is_ok());
    REQUIRE_FALSE(r.is_err());
    REQUIRE(r.value() == 42);
}

TEST_CASE("Create Err result and access error", "[result]") {
    auto r = Result<int>::err(LoomError{LoomError::NotFound, "missing item"});
    REQUIRE(r.is_err());
    REQUIRE_FALSE(r.is_ok());
    REQUIRE(r.error().code == LoomError::NotFound);
    REQUIRE(r.error().message == "missing item");
}

TEST_CASE("Bool conversion", "[result]") {
    auto ok = Result<int>::ok(1);
    auto err = Result<int>::err(LoomError{LoomError::IO, "fail"});
    REQUIRE(static_cast<bool>(ok) == true);
    REQUIRE(static_cast<bool>(err) == false);
}

TEST_CASE("Value access on Err throws bad_variant_access", "[result]") {
    auto r = Result<int>::err(LoomError{LoomError::IO, "fail"});
    REQUIRE_THROWS_AS(r.value(), std::bad_variant_access);
}

TEST_CASE("Error access on Ok throws bad_variant_access", "[result]") {
    auto r = Result<int>::ok(42);
    REQUIRE_THROWS_AS(r.error(), std::bad_variant_access);
}

TEST_CASE("map() transforms Ok value", "[result]") {
    auto r = Result<int>::ok(5);
    auto mapped = r.map([](int x) { return x * 2; });
    REQUIRE(mapped.is_ok());
    REQUIRE(mapped.value() == 10);
}

TEST_CASE("map() passes through Err", "[result]") {
    auto r = Result<int>::err(LoomError{LoomError::Parse, "bad input"});
    bool called = false;
    auto mapped = r.map([&](int x) { called = true; return x * 2; });
    REQUIRE(mapped.is_err());
    REQUIRE_FALSE(called);
    REQUIRE(mapped.error().code == LoomError::Parse);
    REQUIRE(mapped.error().message == "bad input");
}

TEST_CASE("and_then() chains Ok results", "[result]") {
    auto r = Result<int>::ok(5);
    auto chained = r.and_then([](int x) {
        return Result<int>::ok(x + 10);
    });
    REQUIRE(chained.is_ok());
    REQUIRE(chained.value() == 15);
}

TEST_CASE("and_then() short-circuits on Err", "[result]") {
    auto r = Result<int>::err(LoomError{LoomError::Cycle, "loop"});
    bool called = false;
    auto chained = r.and_then([&](int x) {
        called = true;
        return Result<int>::ok(x + 10);
    });
    REQUIRE(chained.is_err());
    REQUIRE_FALSE(called);
    REQUIRE(chained.error().code == LoomError::Cycle);
}

TEST_CASE("or_else() on Ok passes through", "[result]") {
    auto r = Result<int>::ok(5);
    bool called = false;
    auto recovered = r.or_else([&](LoomError&) {
        called = true;
        return Result<int>::ok(99);
    });
    REQUIRE(recovered.is_ok());
    REQUIRE(recovered.value() == 5);
    REQUIRE_FALSE(called);
}

TEST_CASE("or_else() on Err calls recovery", "[result]") {
    auto r = Result<int>::err(LoomError{LoomError::IO, "disk full"});
    auto recovered = r.or_else([](LoomError& e) {
        return Result<int>::ok(0);
    });
    REQUIRE(recovered.is_ok());
    REQUIRE(recovered.value() == 0);
}

TEST_CASE("LOOM_TRY propagates errors", "[result]") {
    auto input = Result<int>::err(LoomError{LoomError::Parse, "syntax error"});
    auto output = try_double(input);
    REQUIRE(output.is_err());
    REQUIRE(output.error().code == LoomError::Parse);
    REQUIRE(output.error().message == "syntax error");
}

TEST_CASE("LOOM_TRY passes through Ok", "[result]") {
    auto input = Result<int>::ok(7);
    auto output = try_double(input);
    REQUIRE(output.is_ok());
    REQUIRE(output.value() == 14);
}

TEST_CASE("LOOM_TRY chained - all Ok", "[result]") {
    auto r = try_chain(false);
    REQUIRE(r.is_ok());
    REQUIRE(r.value() == 15);
}

TEST_CASE("LOOM_TRY chained - first fails", "[result]") {
    auto r = try_chain(true);
    REQUIRE(r.is_err());
    REQUIRE(r.error().message == "first failed");
}

TEST_CASE("Status (void result) Ok", "[result]") {
    auto s = ok_status();
    REQUIRE(s.is_ok());
}

TEST_CASE("Status (void result) Err", "[result]") {
    auto s = Status::err(LoomError{LoomError::Config, "bad config"});
    REQUIRE(s.is_err());
    REQUIRE(s.error().code == LoomError::Config);
}

TEST_CASE("LoomError format() output", "[error]") {
    LoomError e{LoomError::IO, "file not found", "check the path", "main.cpp", 42};
    auto formatted = e.format();
    REQUIRE(formatted.find("error[IO]") != std::string::npos);
    REQUIRE(formatted.find("file not found") != std::string::npos);
    REQUIRE(formatted.find("hint: check the path") != std::string::npos);
    REQUIRE(formatted.find("--> main.cpp:42") != std::string::npos);
}

TEST_CASE("LoomError format() without hint or file", "[error]") {
    LoomError e{LoomError::Parse, "unexpected token"};
    auto formatted = e.format();
    REQUIRE(formatted.find("error[Parse]") != std::string::npos);
    REQUIRE(formatted.find("unexpected token") != std::string::npos);
    REQUIRE(formatted.find("hint:") == std::string::npos);
    REQUIRE(formatted.find("-->") == std::string::npos);
}

TEST_CASE("LoomError code_name() for all codes", "[error]") {
    REQUIRE(std::string(LoomError::code_name(LoomError::IO)) == "IO");
    REQUIRE(std::string(LoomError::code_name(LoomError::Parse)) == "Parse");
    REQUIRE(std::string(LoomError::code_name(LoomError::Version)) == "Version");
    REQUIRE(std::string(LoomError::code_name(LoomError::Dependency)) == "Dependency");
    REQUIRE(std::string(LoomError::code_name(LoomError::Config)) == "Config");
    REQUIRE(std::string(LoomError::code_name(LoomError::Manifest)) == "Manifest");
    REQUIRE(std::string(LoomError::code_name(LoomError::Checksum)) == "Checksum");
    REQUIRE(std::string(LoomError::code_name(LoomError::Network)) == "Network");
    REQUIRE(std::string(LoomError::code_name(LoomError::NotFound)) == "NotFound");
    REQUIRE(std::string(LoomError::code_name(LoomError::Duplicate)) == "Duplicate");
    REQUIRE(std::string(LoomError::code_name(LoomError::Cycle)) == "Cycle");
    REQUIRE(std::string(LoomError::code_name(LoomError::InvalidArg)) == "InvalidArg");
}

TEST_CASE("Result with move-only type (unique_ptr)", "[result]") {
    auto r = Result<std::unique_ptr<int>>::ok(std::make_unique<int>(99));
    REQUIRE(r.is_ok());
    REQUIRE(*r.value() == 99);

    auto r2 = Result<std::unique_ptr<int>>::err(LoomError{LoomError::IO, "fail"});
    REQUIRE(r2.is_err());
}

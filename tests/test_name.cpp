#include <catch2/catch.hpp>
#include <loom/name.hpp>

using namespace loom;

TEST_CASE("parse valid package name", "[name]") {
    auto r = PkgName::parse("my-package");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().raw() == "my-package");
    REQUIRE(r.value().normalized() == "my_package");
}

TEST_CASE("parse name with underscores", "[name]") {
    auto r = PkgName::parse("uart_ip");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().raw() == "uart_ip");
    REQUIRE(r.value().normalized() == "uart_ip");
}

TEST_CASE("parse name with mixed case", "[name]") {
    auto r = PkgName::parse("MyPackage");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().normalized() == "mypackage");
}

TEST_CASE("normalized equality", "[name]") {
    auto a = PkgName::parse("my-package").value();
    auto b = PkgName::parse("my_package").value();
    REQUIRE(a == b);
}

TEST_CASE("case-insensitive equality", "[name]") {
    auto a = PkgName::parse("MyPkg").value();
    auto b = PkgName::parse("mypkg").value();
    REQUIRE(a == b);
}

TEST_CASE("different names not equal", "[name]") {
    auto a = PkgName::parse("foo").value();
    auto b = PkgName::parse("bar").value();
    REQUIRE(a != b);
}

TEST_CASE("empty name error", "[name]") {
    auto r = PkgName::parse("");
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::InvalidArg);
}

TEST_CASE("name starting with digit error", "[name]") {
    auto r = PkgName::parse("123pkg");
    REQUIRE(r.is_err());
}

TEST_CASE("name with spaces error", "[name]") {
    auto r = PkgName::parse("my package");
    REQUIRE(r.is_err());
}

TEST_CASE("name with special chars error", "[name]") {
    auto r = PkgName::parse("my.package");
    REQUIRE(r.is_err());
}

TEST_CASE("single letter name valid", "[name]") {
    auto r = PkgName::parse("a");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().normalized() == "a");
}

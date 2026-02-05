#include <catch2/catch.hpp>
#include <loom/log.hpp>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <io.h>
#define dup _dup
#define dup2 _dup2
#define fileno _fileno
#define read _read
#define write _write
#define close _close
#define pipe _pipe
#else
#include <unistd.h>
#endif

using namespace loom::log;

// Helper: capture stderr output from a callable
static std::string capture_stderr(std::function<void()> fn) {
    // Flush before redirect
    std::fflush(stderr);

    // Save original stderr
    int saved_stderr = dup(fileno(stderr));

    // Create a pipe
    int pipefd[2];
    pipe(pipefd);

    // Redirect stderr to the write end of the pipe
    dup2(pipefd[1], fileno(stderr));
    close(pipefd[1]);

    // Run the function
    fn();

    // Flush and restore stderr
    std::fflush(stderr);
    dup2(saved_stderr, fileno(stderr));
    close(saved_stderr);

    // Read from the pipe
    std::string output;
    char buf[1024];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        output.append(buf, n);
    }
    close(pipefd[0]);

    return output;
}

TEST_CASE("set_level / get_level roundtrip", "[log]") {
    set_level(Trace);
    REQUIRE(get_level() == Trace);

    set_level(Debug);
    REQUIRE(get_level() == Debug);

    set_level(Info);
    REQUIRE(get_level() == Info);

    set_level(Warn);
    REQUIRE(get_level() == Warn);

    set_level(Error);
    REQUIRE(get_level() == Error);
}

TEST_CASE("level_name() returns correct strings", "[log]") {
    REQUIRE(std::string(level_name(Trace)) == "trace");
    REQUIRE(std::string(level_name(Debug)) == "debug");
    REQUIRE(std::string(level_name(Info)) == "info");
    REQUIRE(std::string(level_name(Warn)) == "warn");
    REQUIRE(std::string(level_name(Error)) == "error");
}

TEST_CASE("set_color_enabled / is_color_enabled", "[log]") {
    set_color_enabled(true);
    REQUIRE(is_color_enabled() == true);

    set_color_enabled(false);
    REQUIRE(is_color_enabled() == false);
}

TEST_CASE("Messages below threshold are suppressed", "[log]") {
    set_level(Warn);
    set_color_enabled(false);

    auto output = capture_stderr([] {
        info("should not appear");
    });
    REQUIRE(output.empty());

    // Reset for other tests
    set_level(Info);
}

TEST_CASE("Messages at threshold are emitted", "[log]") {
    set_level(Warn);
    set_color_enabled(false);

    auto output = capture_stderr([] {
        warn("this is a warning");
    });
    REQUIRE(output.find("warn:") != std::string::npos);
    REQUIRE(output.find("this is a warning") != std::string::npos);

    set_level(Info);
}

TEST_CASE("Messages above threshold are emitted", "[log]") {
    set_level(Warn);
    set_color_enabled(false);

    auto output = capture_stderr([] {
        error("this is an error");
    });
    REQUIRE(output.find("error:") != std::string::npos);
    REQUIRE(output.find("this is an error") != std::string::npos);

    set_level(Info);
}

TEST_CASE("Format string substitution", "[log]") {
    set_level(Info);
    set_color_enabled(false);

    auto output = capture_stderr([] {
        info("value: %d, name: %s", 42, "test");
    });
    REQUIRE(output.find("info:") != std::string::npos);
    REQUIRE(output.find("value: 42") != std::string::npos);
    REQUIRE(output.find("name: test") != std::string::npos);
}

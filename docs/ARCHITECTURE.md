# Loom Architecture

> Read this when working on unfamiliar modules or adding new components.
> Update this when project structure changes (new modules, new dependencies between files).

## Module Map

```
CMakeLists.txt              Build system. loom_core library + per-file test targets.
                            loom_add_test() helper sets LOOM_SOURCE_DIR env for fixtures.

include/loom/
  error.hpp                 LoomError struct. 12 error codes, message, hint, file:line.
  result.hpp                Result<T> template. variant<T, LoomError> with monadic ops.
                            LOOM_TRY macro. Status = Result<monostate>. ok_status().
  log.hpp                   loom::log namespace. 5 levels, ANSI color, isatty detection.
  sha256.hpp                SHA256 class. Incremental update/finalize. hash_hex, hash_file.
  uuid.hpp                  Uuid struct. v4 generation, to_string/from_string,
                            encode_base36/decode_base36. 128-bit big-endian arithmetic.
  glob.hpp                  (planned) Glob pattern matching.
  swap.hpp                  (planned) {{ variable }} substitution engine.
  lang/
    verilog/                (planned) Verilog lexer, parser, DST.
    sv/                     (planned) SystemVerilog lexer, parser, DST.

src/util/
  error.cpp                 LoomError::format(), code_name(). Pure string formatting.
  log.cpp                   Global state: s_level, s_color_enabled. va_list printf to stderr.
  sha256.cpp                FIPS 180-4 implementation. K constants, round functions,
                            message schedule, padding. ~190 lines.
  uuid.cpp                  /dev/urandom RNG + mt19937_64 fallback. Hex helpers.
                            Base36 via divide-by-36 / multiply-by-36 on byte array. ~170 lines.

tests/
  test_main.cpp             Catch2 CATCH_CONFIG_MAIN. Compiled once, linked to all tests.
  test_result.cpp           21 cases: ok/err, bool, throws, map, and_then, or_else,
                            LOOM_TRY propagation, Status, format(), code_name(), move-only.
  test_log.cpp              7 cases: levels, names, color toggle, threshold filter,
                            stderr capture via pipe redirect.
  test_sha256.cpp           10 cases: NIST vectors (empty, "abc", 448-bit, 896-bit),
                            incremental, hash_file, bytes_to_hex, 10K-byte input.
  test_uuid.cpp             17 cases: v4 version/variant bits, uniqueness, to_string format,
                            from_string roundtrip + error rejection, base36 roundtrip
                            (random, all-zero, all-0xFF), equality operators.
  fixtures/
    simple_module.v         8-bit counter with clk/rst/en. Used by SHA-256 file hash test.

demos/
  demo_errors.cpp           CLI demo: no-args, bad-path, wrong-ext, happy-path.
                            Chains 4 Result-returning functions with LOOM_TRY.
```

## Dependency Graph (implemented files)

```
result.hpp ──depends on──> error.hpp
log.hpp    ──standalone──
sha256.hpp ──standalone──  (uses <filesystem> for hash_file)
uuid.hpp   ──depends on──> result.hpp (returns Result<Uuid> from from_string, decode_base36)

All src/*.cpp include their corresponding header.
All tests link against loom_core (static lib) + catch2_main (object lib).
```

## Key Patterns

**Error propagation**: Functions return `Result<T>`. Callers use `LOOM_TRY(expr)` to
short-circuit on error. Works across different Result types because `Result<T>` has
an implicit constructor from `LoomError`.

**Test fixtures**: Tests that need files on disk use `LOOM_SOURCE_DIR` env var
(set by CMake `set_tests_properties`) to build absolute paths. Helper function:
```cpp
static std::string fixture_path(const std::string& name) {
    const char* src = std::getenv("LOOM_SOURCE_DIR");
    if (src) return std::string(src) + "/tests/fixtures/" + name;
    return "../tests/fixtures/" + name;
}
```

**Logging**: All output to stderr. Colors auto-detected. Tests capture stderr via
`dup/dup2/pipe` redirect. Always call `set_color_enabled(false)` before capture.

## Build

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && cmake --build .
ctest --output-on-failure          # all tests
./test_sha256 --reporter compact   # single suite, verbose
```

Sanitizer run:
```bash
cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build . --clean-first && ctest --output-on-failure
```

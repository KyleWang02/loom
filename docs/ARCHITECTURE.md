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
  glob.hpp                  Glob pattern matching: glob_match, glob_expand, glob_filter.
                            Supports *, **, ?, [a-z], [!0-9], ! negation prefix.
  swap.hpp                  {{ variable }} substitution engine: swap_template (strict),
                            swap_template_lenient. \{{ escape. Used by EDA drivers
                            (Phase 12) and doc templates (Phase 14).

  target_expr.hpp           TargetExpr class. Boolean filter expressions:
                            all(), any(), not(), bare identifiers, * wildcard.
                            Recursive descent parser + evaluator. TargetSet typedef.
                            SourceGroup struct + filter_source_groups().

  version.hpp               Version (major.minor.micro + label), PartialVersion,
                            ConstraintOp enum, VersionConstraint, VersionReq.
                            Semver constraint parsing (^, ~, >=, <, ranges).
  name.hpp                  PkgName validation ([a-zA-Z][a-zA-Z0-9_-]*) and
                            normalization (lowercase + hyphens to underscores).
  source.hpp                GitSource, PathSource, Dependency structs.
                            Validation: mutual exclusivity, exactly-one-of rules.
  manifest.hpp              Manifest (Loom.toml) parser via toml++. PackageInfo,
                            TargetConfig, LintConfig, BuildConfig, WorkspaceConfig.
                            Parses [package], [dependencies], [[sources]], [lint],
                            [workspace], [targets.*], [build].
  config.hpp                Config with layered loading (global > workspace > local).
                            merge() with explicit _set tracking for boolean fields.

  graph.hpp                 Header-only. Graph<NodeData, EdgeData> with adjacency list.
                            Kahn's topological sort, minimal topo sort from root,
                            has_cycle(), DFS, tree_display with box-drawing chars.
                            GraphMap<EdgeData> string-keyed wrapper.

  lang/
    token.hpp               SourcePos (file, line, col), Token<T> template,
                            CommentKind enum (Line, Block, Doc, Suppression),
                            Comment struct with kind + text + position.
    verilog_token.hpp       VerilogTokenType enum (~90 variants: Verilog keywords,
                            SV keywords, operators, literals, special).
                            verilog_keywords() lookup, verilog_token_name().
    lexer.hpp               LexResult struct (tokens + comments), lex() function.
                            is_sv flag gates SV keyword recognition.

  git.hpp                   (planned) GitCli class. Subprocess wrapper for git operations.
  cache_git.hpp             (planned) CacheManager for git deps. Two-tier cache.
  lockfile.hpp              (planned) Loom.lock TOML format. LockedPackage, LockFile.

  cache.hpp                 (planned) SQLite-based incremental build cache.

  workspace.hpp             (planned) Workspace class. Member discovery, config inheritance.
  project.hpp               (planned) Project detection, loading, source file collection.
  local_override.hpp        (planned) Loom.local parser. Bypasses lockfile.

  resolver.hpp              (planned) DependencyResolver. BFS resolution, conflict detection.

  filelist.hpp              (planned) Filelist generation with target filtering.

  target/                   (planned) EDA tool drivers: 9 built-in + custom.
  lint/                     (planned) Lint engine: 22 rules in 3 categories.
  doc/                      (planned) Documentation generation: extractors + renderers.

src/util/
  error.cpp                 LoomError::format(), code_name(). Pure string formatting.
  log.cpp                   Global state: s_level, s_color_enabled. va_list printf to stderr.
  sha256.cpp                FIPS 180-4 implementation. ~190 lines.
  uuid.cpp                  /dev/urandom RNG + mt19937_64 fallback. Base36 arithmetic. ~170 lines.
  glob.cpp                  Segment-based recursive matching. ~170 lines.
  swap.cpp                  Single-pass scanner for {{ var }}. ~120 lines.
  target_expr.cpp           Recursive descent parser, evaluator, to_string,
                            is_valid_target_name, parse_target_set, filter_source_groups. ~250 lines.
  version.cpp               Version/PartialVersion parsing, comparison, constraint matching. ~300 lines.
  name.cpp                  PkgName validation and normalization. ~50 lines.
  source.cpp                Dependency::validate() with mutual exclusivity checks. ~80 lines.
  manifest.cpp              Full Loom.toml parsing via toml++. ~400 lines.
  config.cpp                Config parsing, merge logic, global_config_path(). ~200 lines.

src/lang/
  lexer.cpp                 ~450-line state machine. Handles identifiers, numbers
                            (decimal/hex/binary/octal/real/x-z), strings, directives,
                            escaped identifiers, line/block/doc/suppression comments,
                            multi-char operators (<=, ==, ===, <<, >>, ->, =>, etc.).

third_party/
  catch2/catch.hpp          Catch2 v2.13.10 single header.
  tomlplusplus/toml.hpp     toml++ v3.4.0 single header (~17,748 lines).
  sqlite3/                  (planned) SQLite amalgamation.

tests/
  test_main.cpp             Catch2 CATCH_CONFIG_MAIN. Compiled once, linked to all tests.
  test_result.cpp           21 cases: ok/err, bool, throws, map, and_then, LOOM_TRY, Status.
  test_log.cpp              7 cases: levels, names, color, threshold, stderr capture.
  test_sha256.cpp           10 cases: NIST vectors, incremental, hash_file, bytes_to_hex.
  test_uuid.cpp             17 cases: v4 bits, uniqueness, string roundtrip, base36 roundtrip.
  test_glob.cpp             18 cases: literal, *, ?, **, char classes, negation, expand.
  test_swap.cpp             16 cases: vars, whitespace, errors, escape, lenient mode.
  test_target_expr.cpp      26 cases: parsing, errors, evaluation, vacuous truth, filtering.
  test_version.cpp          22 cases: parsing, comparison, pre-release, constraints.
  test_name.cpp             11 cases: validation, normalization, equality.
  test_source.cpp           15 cases: GitSource, PathSource, Dependency validation.
  test_manifest.cpp         18 cases: full Loom.toml parsing, workspace, targets, lint.
  test_config.cpp           12 cases: parsing, merge, effective config, boolean tracking.
  test_lexer.cpp            30 cases: all token types, comments, operators, fixtures, edge cases.
  test_graph.cpp            27 cases: basic ops, topo sort, cycles, DFS, tree display, GraphMap.
  bench_lexer.cpp           2 benchmarks: 10K lines <100ms (17ms), 50K lines <500ms (71ms).
  bench_graph.cpp           4 benchmarks: 10K nodes topo/cycle/GraphMap all <50ms (<1ms).
  fixtures/
    simple_module.v         8-bit counter. Used by SHA-256 file hash test.
    counter.v               Verilog module with doc comments, suppression, params, always block.
    package_example.sv      SV fixture: package, interface, modport, always_comb, always_ff.
    Loom.toml.example       Full realistic manifest fixture for manifest parser tests.
    workspace.toml.example  Workspace manifest fixture for workspace parsing tests.

demos/
  demo_errors.cpp           CLI demo: chains 4 Result-returning functions with LOOM_TRY.

docs/
  research/                 Detailed feature specifications from research agents.
    loom_doc_specification.md  Documentation generation spec.
```

## Dependency Graph (implemented files)

```
result.hpp ──depends on──> error.hpp
log.hpp    ──standalone──
sha256.hpp ──standalone──  (uses <filesystem> for hash_file)
uuid.hpp   ──depends on──> result.hpp
glob.hpp   ──depends on──> result.hpp
swap.hpp   ──depends on──> result.hpp
target_expr.hpp ──depends on──> result.hpp
version.hpp ──depends on──> result.hpp
name.hpp    ──depends on──> result.hpp
source.hpp  ──depends on──> result.hpp, version.hpp, name.hpp
manifest.hpp ──depends on──> result.hpp, version.hpp, name.hpp, source.hpp, target_expr.hpp
config.hpp  ──depends on──> result.hpp
graph.hpp   ──depends on──> result.hpp
lang/token.hpp ──standalone──
lang/verilog_token.hpp ──standalone──
lang/lexer.hpp ──depends on──> token.hpp, verilog_token.hpp, result.hpp

All src/*.cpp include their corresponding header.
All tests link against loom_core (static lib) + catch2_main (object lib).
manifest.cpp uses third_party/tomlplusplus/toml.hpp.
config.cpp uses third_party/tomlplusplus/toml.hpp.
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

**Target filtering**: Source files are grouped into `SourceGroup` entries, each with
an optional `TargetExpr`. At build time, groups are filtered against the active target
set (from `--target` CLI flag). Only matching groups' files enter the filelist pipeline.

**TOML parsing**: Uses toml++ v3.4.0 single header. Manifest and Config both parse
TOML tables. Use `R"TOML(...)TOML"` delimiter for raw string literals containing
parentheses in test fixtures.

**Header-only templates**: `Graph<NodeData, EdgeData>` is header-only to avoid
explicit template instantiation. ~285 lines including GraphMap wrapper.

**Lexer state machine**: Character-level lexer with `is_sv` flag gating SV keyword
recognition. Preserves `///` doc comments and `// loom: ignore[...]` suppression
comments for downstream phases (lint, doc generation).

**Config merge**: Layered config (global > workspace > local) with explicit `_set`
tracking flags for boolean fields to distinguish "not mentioned" from "set to false".

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

Performance benchmarks (Release mode):
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build .
./bench_lexer -s    # lexer: 10K lines, 50K lines
./bench_graph -s    # graph: topo sort, cycle detection, GraphMap
```

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

  target_expr.hpp           (planned) TargetExpr class. Boolean filter expressions:
                            all(), any(), not(), bare identifiers, * wildcard.
                            Recursive descent parser + evaluator.
  source_group.hpp          (planned) SourceGroup struct. Files + optional TargetExpr +
                            include_dirs + defines. filter_source_groups().

  version.hpp               (planned) Version (major.minor.micro + label), PartialVersion,
                            semver constraint parsing (^, ~, >=, <, ranges).
  name.hpp                  (planned) PkgName validation and normalization.
  manifest.hpp              (planned) Manifest (Loom.toml) parser. [package], [dependencies],
                            [[sources]], [lint], [workspace], [targets.*], [build].
  config.hpp                (planned) Config with layered loading (global > workspace > local).

  git.hpp                   (planned) GitCli class. Subprocess wrapper for git operations:
                            ls_remote, clone_bare, fetch, checkout, resolve_ref, show_file.
  source.hpp                (planned) DependencySource variant: GitSource | PathSource.
                            GitSource: url + tag/version/rev/branch.
  cache_git.hpp             (planned) CacheManager for git deps. Two-tier cache:
                            bare repos in ~/.loom/cache/git/db/,
                            checkouts in ~/.loom/cache/git/checkouts/.
  lockfile.hpp              (planned) Loom.lock TOML format. LockedPackage, LockFile.

  cache.hpp                 (planned) SQLite-based incremental build cache.
                            file_stat, parse_result, include_dep, dep_edge, filelist tables.
                            4-layer hashing: content → parse → effective → filelist.
                            Stat-based fast path (inode + mtime + size).

  workspace.hpp             (planned) Workspace class. Virtual and root-package types.
                            Member discovery via glob, single Loom.lock, config inheritance.
  project.hpp               (planned) Project detection, loading, source file collection.
  local_override.hpp        (planned) Loom.local parser. OverrideSource (path/git).
                            Bypasses lockfile without modifying it.

  resolver.hpp              (planned) DependencyResolver. BFS resolution, conflict detection,
                            semver tag matching, workspace integration.

  filelist.hpp              (planned) Filelist generation with target filtering.
                            Topological sort, top-module detection, testbench heuristic.

  target/
    types.hpp               (planned) Filelist, SourceFile, ToolAction, ToolResult, ToolOptions.
    tool_driver.hpp         (planned) ToolDriver abstract base class + registry.
    driver_icarus.hpp       (planned) Icarus Verilog: iverilog/vvp.
    driver_verilator.hpp    (planned) Verilator: lint + simulate.
    driver_vivado_sim.hpp   (planned) Vivado Simulator: xvlog/xelab/xsim.
    driver_vivado_synth.hpp (planned) Vivado Synthesis: TCL batch.
    driver_quartus.hpp      (planned) Intel Quartus Prime: TCL.
    driver_modelsim.hpp     (planned) ModelSim/QuestaSim: TCL + DO.
    driver_vcs.hpp          (planned) Synopsys VCS: file list.
    driver_xcelium.hpp      (planned) Cadence Xcelium: xrun.
    driver_yosys.hpp        (planned) Yosys: .ys script.
    driver_custom.hpp       (planned) Custom: user-defined commands with {{ }} substitution.

  lint/
    lint_rule.hpp           (planned) LintRule base class, Severity enum, Diagnostic struct.
    lint_engine.hpp         (planned) LintEngine: load config, run rules, collect diagnostics.
    lint_config.hpp         (planned) LintConfig: parse [lint] from Loom.toml.
    lint_suppression.hpp    (planned) SuppressionMap: // loom: ignore[rule-id] comments.
    rules/                  (planned) 22 rules: 11 correctness, 8 structure, 3 style.

  doc/
    doc_comment.hpp         (planned) DocTag, DocComment structs. /// comment parsing.
    doc_model.hpp           (planned) DocModel IR: PortDoc, ParamDoc, DesignUnitDoc, CrossRef.
    doc_extractor.hpp       (planned) DocExtractor: walk tokens, associate comments with units.
    renderer.hpp            (planned) Renderer base class + RenderConfig.
    markdown_renderer.hpp   (planned) Markdown output with Mermaid dependency graphs.
    html_renderer.hpp       (planned) Static HTML site with search, sidebar, diagrams.
    template_engine.hpp     (planned) Lightweight template engine built on Swap.

  lang/
    verilog/                (planned) Verilog lexer, parser, symbol remapping.
    sv/                     (planned) SystemVerilog lexer, parser, symbol remapping.

src/util/
  error.cpp                 LoomError::format(), code_name(). Pure string formatting.
  log.cpp                   Global state: s_level, s_color_enabled. va_list printf to stderr.
  sha256.cpp                FIPS 180-4 implementation. K constants, round functions,
                            message schedule, padding. ~190 lines.
  uuid.cpp                  /dev/urandom RNG + mt19937_64 fallback. Hex helpers.
                            Base36 via divide-by-36 / multiply-by-36 on byte array. ~170 lines.
  glob.cpp                  Segment-based recursive matching. normalize_path, split_segments,
                            match_segment (two-pointer with backtracking), match_segments
                            (handles **). glob_expand via recursive_directory_iterator. ~170 lines.
  swap.cpp                  Single-pass scanner for {{ var }}. Whitespace-trimmed keys,
                            \{{ escape, strict (errors) vs lenient (passthrough). ~120 lines.

src/target/                 (planned) ToolDriver implementations + registry.
src/lint/                   (planned) LintEngine + 22 rule implementations.
src/doc/                    (planned) DocExtractor, renderers, template engine.
src/git/                    (planned) GitCli, CacheManager, resolver, lockfile.

third_party/
  sqlite3/                  (planned) SQLite amalgamation: sqlite3.h + sqlite3.c (~250 KB).

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
  test_glob.cpp             18 cases: literal matching, *, ?, **, char classes [a-z]/[!0-9],
                            negation, glob_filter include+exclude, filesystem expand.
  test_swap.cpp             16 cases: single/multi/no vars, whitespace, errors (undefined,
                            unclosed, empty name, hint), \{{ escape, no recursion,
                            adjacent vars, lenient mode.
  fixtures/
    simple_module.v         8-bit counter with clk/rst/en. Used by SHA-256 file hash test.

demos/
  demo_errors.cpp           CLI demo: no-args, bad-path, wrong-ext, happy-path.
                            Chains 4 Result-returning functions with LOOM_TRY.

docs/
  research/                 Detailed feature specifications from research agents.
    loom_doc_specification.md  Documentation generation: DocModel, renderers, templates.
```

## Dependency Graph (implemented files)

```
result.hpp ──depends on──> error.hpp
log.hpp    ──standalone──
sha256.hpp ──standalone──  (uses <filesystem> for hash_file)
uuid.hpp   ──depends on──> result.hpp (returns Result<Uuid> from from_string, decode_base36)
glob.hpp   ──depends on──> result.hpp (glob_expand returns Result)
swap.hpp   ──depends on──> result.hpp (swap_template returns Result)

All src/*.cpp include their corresponding header.
All tests link against loom_core (static lib) + catch2_main (object lib).
```

## Planned Dependency Graph (full project)

```
Foundation:
  result.hpp ──> error.hpp
  uuid.hpp ──> result.hpp
  target_expr.hpp ──> result.hpp
  source_group.hpp ──> target_expr.hpp

Manifest/Config:
  manifest.hpp ──> version.hpp, name.hpp, source_group.hpp, result.hpp
  config.hpp ──> result.hpp

Git/Dependencies:
  git.hpp ──> result.hpp
  cache_git.hpp ──> git.hpp, sha256.hpp
  lockfile.hpp ──> result.hpp
  resolver.hpp ──> git.hpp, lockfile.hpp, manifest.hpp, workspace.hpp

Build Cache:
  cache.hpp ──> result.hpp, sha256.hpp (+ third_party/sqlite3)

Workspace/Project:
  workspace.hpp ──> manifest.hpp, glob.hpp, result.hpp
  local_override.hpp ──> result.hpp
  project.hpp ──> manifest.hpp, workspace.hpp

Filelist/Targets:
  filelist.hpp ──> source_group.hpp, cache.hpp
  target/tool_driver.hpp ──> filelist.hpp, result.hpp
  target/driver_custom.hpp ──> tool_driver.hpp, swap.hpp

Lint/Doc:
  lint/lint_engine.hpp ──> lint_rule.hpp, lint_config.hpp, lint_suppression.hpp
  doc/doc_extractor.hpp ──> doc_comment.hpp, doc_model.hpp
  doc/template_engine.hpp ──> swap.hpp
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

**Git dependencies**: All dependency sources are explicit (git URL or local path).
No central registry. Loom shells out to the `git` CLI for all operations, inheriting
the user's SSH keys, credential helpers, and `.gitconfig`.

**Incremental cache**: SQLite-based content-addressed cache. Stat metadata (inode +
mtime + size) provides a fast path to skip SHA-256 hashing of unchanged files.
Parse results and filelists are cached by content hash.

**Loom.local overrides**: Developer-local overrides that bypass the lockfile without
modifying it. Loom.local is gitignored by default and emits a warning when active.

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

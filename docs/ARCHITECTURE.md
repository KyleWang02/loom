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

  git.hpp                   GitCli class. Subprocess wrapper for git operations.
                            check_version, ls_remote, clone_bare, fetch, checkout,
                            resolve_ref, show_file. run_command() with timeout.
  cache.hpp                 CacheManager for git deps. Two-tier cache:
                            bare repos in ~/.loom/cache/git/db/, working trees in
                            checkouts/. ensure_bare_repo, ensure_checkout, compute_checksum.
  lockfile.hpp              Loom.lock TOML format. LockedPackage, LockFile.
                            load(), save(), is_stale(), find().

  build_cache.hpp           BuildCache class with pImpl pattern. SQLite-backed
                            incremental build cache in .loom/cache/loom_cache.db.
                            6 tables: schema_info, file_stat, parse_result,
                            include_dep, dep_edge, filelist. Custom binary
                            serialization (LPR\x01 magic + varint encoding).
                            14 prepared statements. WAL mode. Stat-based fast path.
                            compute_effective_hash(), compute_filelist_key().

  workspace.hpp             Workspace class. discover(), load(), expand_member_globs(),
                            find_member(), resolve_targets(), resolve_workspace_dep(),
                            resolve_member_dep(), effective_config(), validate().
  project.hpp               Project detection, loading, source file collection.
  local_override.hpp        Loom.local parser. OverrideSource (Path/Git), LocalOverrides.
                            load(), parse(), validate(), warn_active(), discover_local_overrides().

  resolver.hpp              DependencyResolver class. BFS resolution, conflict detection.
                            resolve(), update(), resolve_workspace(), apply_overrides(),
                            topological_sort(). ResolveOptions, ResolvedPackage structs.

  filelist.hpp              FilelistGenerator class. Pipeline: parse_all_files →
                            build_unit_graph → build_file_graph → topo_sort → detect.
                            FilelistResult with providers-first file list, to_dot_f(),
                            to_json(). Top module detection, black box detection,
                            testbench heuristics. Optional BuildCache* for caching.

  tool_types.hpp            ToolAction enum (Lint/Simulate/Synthesize/Build),
                            ToolResult, ToolOptions (from_map with CSV parsing),
                            ToolCommand structs.
  tool_driver.hpp           ToolDriver abstract base class. Command generation (pure)
                            vs execution (subprocess). 10 drivers: Icarus, Verilator,
                            VivadoSim, VivadoSynth, Quartus, ModelSim, VCS, Xcelium,
                            Yosys, Custom. Factory: create_driver(), detect_driver().

  lint.hpp                  namespace loom::lint. Severity, LintDiagnostic, LintRule
                            (abstract base with 4 check levels), SuppressionMap,
                            LintReport, LintEngine. 22 rules (19 active, 3 stubs).
                            configure(), lint_file(), lint_parsed(), lint_files().
  doc.hpp                   namespace loom::doc. DocTag (8 kinds), DocComment,
                            DocExtractor, PortDoc, ParamDoc, DesignUnitDoc,
                            DocModel (cross-ref resolution), MarkdownRenderer
                            (index.md, per-unit pages, Mermaid graphs, search_index.json).
  sr.hpp                    SymbolRemapper class. detect_collisions(), mangle(),
                            build_remap_table(), remap_tokens(), remap_parse_result().
                            SymbolCollision, SymbolOrigin, SymbolMap, FileRemapTable.
                            format_report() (GCC-style), format_json().
  util.hpp                  FileLock (RAII flock wrapper), Progress (TTY-aware status),
                            signal handling (install_signal_handlers, is_interrupted).
  cli.hpp                   CLI framework: Flag, CliArgs, Command, CliParser.
                            levenshtein() for fuzzy matching. Two-phase parsing.
                            15 command register_*() forward declarations.

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
  workspace.cpp             Workspace discovery, loading, member expansion, config inheritance. ~415 lines.
  local_override.cpp        Loom.local TOML parser, validation, discovery. ~185 lines.
  project.cpp               Project detection, loading, checksum. ~140 lines.
  resolver.cpp              BFS dependency resolution, git/path resolution, lockfile building. ~396 lines.
  filelist.cpp              Filelist generation pipeline. ~450 lines.
  tool_driver.cpp           10 EDA tool drivers + factory + options parsing. ~500 lines.
  lint.cpp                  19 active lint rules + 3 deferred stubs. SuppressionMap,
                            LintEngine orchestrator. ~520 lines.
  doc.cpp                   Doc comment parsing, DocExtractor, DocModel cross-refs,
                            MarkdownRenderer with index + per-unit pages + Mermaid.

  util.cpp                  FileLock (flock), Progress (stderr), signal handling (sigaction).

src/cli/
  cli.cpp                   CLI framework: CliParser, CliArgs, Command, parse_flags,
                            levenshtein distance, help text generation. ~440 lines.
  cmd_new.cpp               loom new <name> — scaffold project with Loom.toml + .gitignore.
  cmd_init.cpp              loom init [--workspace] — initialize in existing directory.
  cmd_info.cpp              loom info — display project/workspace metadata.
  cmd_env.cpp               loom env [--tools] — show environment and EDA tool versions.
  cmd_config.cpp            loom config [key] [value] — view/set configuration.
  cmd_lock.cpp              loom lock — resolve dependencies, write Loom.lock.
  cmd_update.cpp            loom update [package] + loom fetch — re-resolve and download deps.
  cmd_tree.cpp              loom tree — display dependency tree.
  cmd_clean.cpp             loom clean [--all] — remove build cache or entire .loom/.
  cmd_build.cpp             loom build + loom test — full build pipeline with EDA driver.
  cmd_plan.cpp              loom plan — generate filelist without executing EDA tool.
  cmd_lint.cpp              loom lint [files...] — lint with severity/format/rule flags.
  cmd_doc.cpp               loom doc — generate documentation from sources.

src/main.cpp                Entry point. Creates CliParser, registers 5 global flags + 15 commands.

src/lang/
  verilog_sr.cpp            Symbol remapping: collision detection, SHA-256 name mangling,
                            token-stream replacement, parse result remapping. ~130 lines.
  lexer.cpp                 ~450-line state machine. Handles identifiers, numbers
                            (decimal/hex/binary/octal/real/x-z), strings, directives,
                            escaped identifiers, line/block/doc/suppression comments,
                            multi-char operators (<=, ==, ===, <<, >>, ->, =>, etc.).
  parser.cpp                Verilog/SV parser. Extracts DesignUnit structs with ports,
                            params, instantiations, imports, always blocks, case
                            statements, signals, generate blocks, labeled blocks.
                            Error recovery. Produces ParseResult.

third_party/
  catch2/catch.hpp          Catch2 v2.13.10 single header.
  tomlplusplus/toml.hpp     toml++ v3.4.0 single header (~17,748 lines).
  sqlite3/sqlite3.{h,c}    SQLite v3.45.0 amalgamation. Built as static C library.

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
  test_parser.cpp           Parser tests: module/package/interface extraction, ports, params,
                            instantiations, always blocks, case statements, signals, generates.
  test_git.cpp              GitCli tests: version check, ls_remote, cache directory naming.
  test_lockfile.cpp         Lockfile tests: roundtrip, staleness detection, find.
  test_build_cache.cpp      25 cases: stat cache, parse roundtrip (empty, full, nested),
                            include/edge tracking, filelist cache, hash helpers,
                            prune, clear, stats, schema migration, corruption recovery.
  test_project.cpp          Project detection, loading, source collection.
  test_workspace.cpp        Workspace discovery, member expansion, config inheritance,
                            resolve_targets, resolve_workspace_dep, resolve_member_dep.
  test_local_override.cpp   Loom.local parsing, validation, discovery, suppression.
  test_resolver.cpp         26 cases: single dep (tag/version/rev/branch/path), transitive
                            (two-level, diamond, deep chain, mixed), lockfile reuse/stale,
                            selective update, conflict detection, overrides, topo sort, edge cases.
  test_filelist.cpp         30 cases: graph construction, topo sort, top modules, black boxes,
                            testbenches, output formats, target filtering, caching.
  test_tool_driver.cpp      39 cases: action parsing, options parsing, factory, driver identity,
                            command generation for all 10 drivers, helpers, resolve_top_module.
  test_lint.cpp             60 cases: all 19 active rules, suppression, config overrides,
                            JSON output, project-level duplicate-module. 213 assertions.
  test_doc.cpp              50 cases: doc comment parsing, extraction, cross-references,
                            Markdown rendering, disk output. 273 assertions.
  test_cli.cpp              25 cases: levenshtein, CliArgs, CliParser (help, version,
                            fuzzy suggestions, dispatch, flags, positional, pass-through).
  test_sr.cpp               27 cases: collision detection, name mangling, token remapping,
                            parse result remapping, report formatting, full pipeline.
  test_integration.cpp      15 cases: project discover, workspace, filelist pipeline,
                            target filtering, build cache, lint, doc, SR, overrides,
                            lockfile roundtrip, file locking, signal handling.
  bench_lexer.cpp           2 benchmarks: 10K lines <100ms (17ms), 50K lines <500ms (71ms).
  bench_graph.cpp           4 benchmarks: 10K nodes topo/cycle/GraphMap all <50ms (<1ms).
  bench_parser.cpp          Parser performance benchmark.
  fixtures/
    simple_module.v         8-bit counter. Used by SHA-256 file hash test.
    counter.v               Verilog module with doc comments, suppression, params, always block.
    package_example.sv      SV fixture: package, interface, modport, always_comb, always_ff.
    Loom.toml.example       Full realistic manifest fixture for manifest parser tests.
    workspace.toml.example  Workspace manifest fixture for workspace parsing tests.

demos/
  demo_errors.cpp           CLI demo: chains 4 Result-returning functions with LOOM_TRY.
  loom_demo.cpp             Real-world CLI harness: loom-demo lint/filelist/resolve.
                            Accepts files/dirs, recursively scans for .v/.sv.
                            Flags: --json, --rule, --config, --top, --save.
  samples/
    lint/                   10 .sv/.v files targeting specific lint rules.
    filelist/               6-file module hierarchy for topological sort testing.
    resolve/                setup.sh + myproject with 3 local git repo deps.

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
lang/ir.hpp ──depends on──> token.hpp (for SourcePos)
lang/parser.hpp ──depends on──> ir.hpp, lexer.hpp, result.hpp
git.hpp ──depends on──> result.hpp
cache.hpp ──depends on──> result.hpp, git.hpp, sha256.hpp
lockfile.hpp ──depends on──> result.hpp, source.hpp
workspace.hpp ──depends on──> result.hpp, manifest.hpp, config.hpp
local_override.hpp ──depends on──> result.hpp
resolver.hpp ──depends on──> result.hpp, cache.hpp, manifest.hpp, lockfile.hpp, workspace.hpp, local_override.hpp, graph.hpp
build_cache.hpp ──depends on──> result.hpp, ir.hpp (pImpl hides sqlite3.h)
filelist.hpp ──depends on──> result.hpp, target_expr.hpp, ir.hpp, build_cache.hpp, graph.hpp
tool_types.hpp ──depends on──> result.hpp
tool_driver.hpp ──depends on──> tool_types.hpp, filelist.hpp, manifest.hpp, swap.hpp
lint.hpp ──depends on──> result.hpp, ir.hpp, lexer.hpp, manifest.hpp (for LintConfig)
doc.hpp ──depends on──> result.hpp, ir.hpp, lexer.hpp
sr.hpp ──depends on──> result.hpp, ir.hpp, lexer.hpp
util.hpp ──standalone── (POSIX flock, signals)
cli.hpp ──depends on──> result.hpp

All src/*.cpp include their corresponding header.
All tests link against loom_core (static lib) + catch2_main (object lib).
loom_core links against sqlite3 (static C lib).
manifest.cpp uses third_party/tomlplusplus/toml.hpp.
config.cpp uses third_party/tomlplusplus/toml.hpp.
lockfile.cpp uses third_party/tomlplusplus/toml.hpp.
local_override.cpp uses third_party/tomlplusplus/toml.hpp.
build_cache.cpp uses third_party/sqlite3/sqlite3.h (internal only, not in public header).
resolver.cpp uses cache.hpp, git.hpp, manifest.hpp, lockfile.hpp, workspace.hpp, local_override.hpp, graph.hpp.
tool_driver.cpp uses git.hpp (for run_command), swap.hpp, filelist.hpp, manifest.hpp.
lint.cpp uses parser.hpp (qualified as loom::parse()), lexer.hpp (loom::lex()), manifest.hpp (LintConfig).
doc.cpp uses parser.hpp, lexer.hpp, ir.hpp, <filesystem> for render() output.
verilog_sr.cpp uses ir.hpp, lexer.hpp, sha256.hpp.
util.cpp uses log.hpp, <sys/file.h>, <csignal>, <atomic>.
cli.cpp uses cli.hpp, log.hpp.
cmd_*.cpp use cli.hpp and relevant module headers.
main.cpp uses cli.hpp and calls all register_*() functions.
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

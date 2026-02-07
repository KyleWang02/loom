# Loom Implementation Plan

## Checkpoint Workflow

Each phase uses a strict implement-test-review loop:

1. **Implement** a testable chunk → **STOP** → present test plan to user
2. User approves test plan → **write & run tests** → fix failures
3. **STOP** → present review (what was done, test results, run commands)
4. User runs tests, gives feedback → iterate or proceed
5. Between phases: all tests pass, user confirms, `/handoff` updates docs

See `CLAUDE.md` "Checkpoint Workflow" section for full details.

## Phase Tracking

- [x] Phase 0: Project Scaffolding and Session Management ← DONE
- [x] Phase 1: Foundation Layer (Result, SHA256, UUID, Glob, Swap, Log) ← DONE
- [x] Phase 2: Target Expression Parser ← DONE
- [x] Phase 3: Manifest, Configuration, and Versioning ← DONE
- [x] Phase 4: Verilog/SystemVerilog Lexer and Tokenizer ← DONE
- [x] Phase 5: Verilog/SystemVerilog Parser and Design Unit Extraction ← DONE
- [x] Phase 6: Graph Data Structures and Algorithms ← DONE
- [x] Phase 7: Git Dependencies and Cache Manager ← DONE
- [x] Phase 8: Incremental Build Cache (SQLite) ← DONE
- [ ] Phase 9: Workspace, Project Model, and Local Overrides ← NEXT UP
- [ ] Phase 10: Dependency Resolution and Lockfile
- [ ] Phase 11: Filelist Generation (with Target Filtering)
- [ ] Phase 12: EDA Tool Drivers
- [ ] Phase 13: Lint Engine
- [ ] Phase 14: Documentation Generation
- [ ] Phase 15: CLI Interface and Commands
- [ ] Phase 16: Integration, Symbol Remapping, and Polish

## Phase Dependency Map

```
Phase 1 (Foundation)
  ├──> Phase 2 (Target Expressions) ──┐
  ├──> Phase 4 (Lexer) ──┐            │
  ├──> Phase 6 (Graph) ──┤            │
  ├──> Phase 7 (Git Deps) ──┐         │
  ├──> Phase 8 (Build Cache) │         │
  │                       │  │         v
  │                       v  │   Phase 3 (Manifest, Config)
  │               Phase 5 (Parser)     │
  │                  │  │  │           │
  │                  │  │  │     ┌─────┘
  │                  │  │  │     │
  │                  │  │  │     v
  │                  │  │  │  Phase 9 (Workspace, Project, Loom.local)
  │                  │  │  │     │
  │                  │  │  │     ├──> Phase 10 (Dep Resolution) ◄── Phase 7
  │                  │  │  │     │         │
  │                  │  │  │     │         v
  │                  │  │  └──> Phase 11 (Filelist) ◄── Phase 6
  │                  │  │                  │
  │                  │  │                  v
  │                  │  │         Phase 12 (EDA Drivers) ◄── Phase 1 (Swap)
  │                  │  │
  │                  │  ├──> Phase 13 (Lint)
  │                  │  └──> Phase 14 (Documentation)
  │                  │
  │                  └──────────────────────────────┐
  │                                                 v
  └──────────────────────────────────────> Phase 15 (CLI)
                                                    │
                                                    v
                                            Phase 16 (Integration, SR)
```

Parallelizable groups after Phase 1:
- **Group A**: Phases 2, 4, 6, 7, 8 (all depend only on Phase 1)
- **Group B**: Phase 3 (after Phase 2), Phase 5 (after Phase 4)
- **Group C**: Phase 9 (after Phases 3, 7), Phases 13 and 14 (after Phase 5)
- **Group D**: Phase 10 (after Phases 7, 9), Phase 11 (after Phases 5, 6, 10)
- **Group E**: Phase 12 (after Phase 11)

## What Changed from the Original Plan:

| Change | Rationale |
|--------|-----------|
| **Channels removed** → git-direct dependencies | Simpler, no registry infrastructure. Every dep specifies its source explicitly. Based on Bender/Cargo. |
| **Archive/distribution removed** → git cache | Two-tier cache (bare repo + checkout) replaces zip archives. Based on Cargo's git cache design. |
| **Target expressions added** | Boolean filter expressions (`all()`, `any()`, `not()`) on source groups. Based on Bender's proven design. |
| **SQLite incremental cache added** | Content-addressed parse result caching with stat-based fast path. Based on ccache/Bazel/VUnit patterns. |
| **Workspace support added** | Monorepo-friendly: shared deps, single lockfile, inter-member refs. Based on Cargo workspaces. |
| **Loom.local overrides added** | Local dev overrides that bypass lockfile without modifying it. Based on Bender.local/Cargo [patch]. |
| **Lint engine added** | 22 lightweight rules using existing parser AST. Based on Verilator warnings + Verible rules. |
| **Documentation generation added** | `///` doc comments, Markdown/HTML output, Mermaid diagrams. No existing open-source tool covers this for Verilog/SV. |
| **EDA tool drivers added** | Built-in drivers for 9 tools (iverilog, verilator, vivado, quartus, modelsim, vcs, xcelium, yosys) + custom. Based on Edalize's EDAM pattern. |
| **New dependency: SQLite** | Amalgamation in `third_party/sqlite3/`. Single `.c` + `.h` file, ~250 KB, no external install. |

## Phase Details

### Phase 0: Project Scaffolding and Session Management

- [x] Initialize git repository
- [x] Create `CLAUDE.md` with project summary and rules
- [x] Create `docs/PLAN.md` with full roadmap
- [x] Create `docs/SESSION_LOG.md` for rolling journal
- [x] Create `.claude/commands/handoff.md` slash command
- [x] Create directory structure scaffolding
- [x] Create `.gitignore`
- [x] Create `CMakeLists.txt` skeleton

### Phase 1: Foundation Layer

Files: `result.hpp`, `error.hpp`, `log.hpp`, `sha256.hpp`, `uuid.hpp`, `glob.hpp`, `swap.hpp` + implementations + tests

- [x] Implement `LoomError` struct with error codes, message, hint, file, line
- [x] Implement `Result<T, LoomError>` with `ok()`, `value()`, `error()`, monadic ops
- [x] Implement `LOOM_TRY` macro for error propagation
- [x] Implement `Status` type alias for void-returning functions
- [x] Implement logging with levels (Trace, Debug, Info, Warn, Error) and ANSI colors
- [x] Implement SHA-256 from NIST FIPS 180-4 (~200 lines)
- [x] Implement UUID v4 generation + base36 encoding/decoding
- [x] Implement glob pattern matching (`*`, `**`, `?`, `[a-z]`, negation)
- [x] Implement `{{ variable }}` swap/substitution engine (also used by EDA drivers and doc templates)
- [x] Set up Catch2 integration in CMake
- [x] Write tests for glob and swap
- [x] Verify all NIST SHA-256 test vectors pass
- [x] Verify UUID base36 roundtrip is lossless
- [x] Run under AddressSanitizer

### Phase 2: Target Expression Parser

Files: `target_expr.hpp`, `source_group.hpp`, `target_expr.cpp` + tests

Implements boolean filter expressions for conditional source file inclusion, adopted from Bender's proven grammar: `all()`, `any()`, `not()`, bare identifiers, and `*` wildcard.

- [x] Define `TargetExpr` class with `Kind` enum (`Wildcard`, `Identifier`, `All`, `Any`, `Not`)
- [x] Implement static constructors: `wildcard()`, `identifier()`, `all()`, `any()`, `negation()`
- [x] Implement `TargetExpr::parse()` — recursive descent parser for expression strings
- [x] Implement `TargetExpr::evaluate(const TargetSet& active)` — evaluate against active target set
- [x] Implement `TargetExpr::to_string()` — canonical serialization
- [x] Define `TargetSet = std::unordered_set<std::string>`
- [x] Implement `parse_target_set()` — parse comma-separated CLI target string
- [x] Implement `is_valid_target_name()` — validation regex `[a-zA-Z][a-zA-Z0-9_-]*`
- [x] Define `SourceGroup` struct with `optional<TargetExpr> target`, `include_dirs`, `defines`, `files`
- [x] Implement `filter_source_groups()` — filter groups by active target set
- [x] Write tests: parsing (bare, wildcard, nested, whitespace, errors), evaluation (all combinations, empty set, vacuous truth), roundtrip serialization, target name validation, CLI set parsing, source group filtering
- [x] Verify `all()` with no children = true (vacuous truth), `any()` with no children = false

**Grammar**:
```
expr       = all_expr | any_expr | not_expr | wildcard | identifier
all_expr   = "all" "(" expr_list ")"
any_expr   = "any" "(" expr_list ")"
not_expr   = "not" "(" expr ")"
expr_list  = expr { "," expr }
wildcard   = "*"
identifier = [a-zA-Z][a-zA-Z0-9_-]*
```

**Loom.toml usage** (parsed in Phase 3):
```toml
[[sources]]
target = "all(simulation, not(verilator))"
files = ["tb/tb_sram_model.sv"]
```

### Phase 3: Manifest, Configuration, and Versioning

Files: `version.hpp`, `name.hpp`, `manifest.hpp`, `config.hpp`, `source.hpp` + implementations + tests

Expanded from original to support git/path dependency sources, `[[sources]]` with target filter expressions, `[lint]` configuration, and `[workspace]` sections. Uses toml++ for parsing.

- [x] Implement `Version` (major.minor.micro + optional label) with parsing and comparison
- [x] Implement `PartialVersion` with compatibility checking
- [x] Implement semver constraint parsing (`^`, `~`, `>=`, `<`, ranges)
- [x] Implement `PkgName` with validation and normalization
- [x] Define `DependencySource` variant: `GitSource` (url + tag/version/rev/branch) | `PathSource`
- [x] Implement `Dependency` struct with source validation rules:
  - Exactly one of `{tag, version, rev, branch}` when `git` is present
  - `git` and `path` are mutually exclusive
  - `version` without `git` is an error (no registry)
  - `{ workspace = true }` for workspace dependency inheritance
  - `{ member = true }` for inter-workspace-member references
- [x] Implement `Manifest` (Loom.toml) parsing with toml++:
  - `[package]` section: name, version, top, authors
  - `[dependencies]` section: git/path/workspace/member sources
  - `[[sources]]` array-of-tables: `SourceGroup` with optional `target` filter expression
  - `[lint]` section: rule severity overrides + `[lint.naming]` regex patterns
  - `[workspace]` section: members, exclude, default-members, `[workspace.dependencies]`
  - `[targets.<name>]` sections: tool, action, options, custom commands
  - `[build]` section: pre-lint, lint-fatal options
- [x] Implement `Config` with layered loading (global > workspace > local)
- [x] Implement config merge rules (member overrides workspace)
- [x] Write test fixtures (`Loom.toml.example`, `config.toml.example`)
- [x] Write tests for all components
- [ ] Verify manifest roundtrip (load -> save -> load)

**Dependency specification examples**:
```toml
[dependencies]
uart_ip    = { git = "https://github.com/org/uart.git", tag = "v1.3.0" }
axi_bus    = { git = "https://github.com/org/axi.git", version = ">=2.0.0, <3.0.0" }
crypto     = { git = "ssh://git@internal.corp/crypto.git", rev = "abc123def456" }
spi_dev    = { git = "https://github.com/org/spi.git", branch = "develop" }
my_testbench = { path = "../testbench" }
common_cells = { workspace = true }
sibling_ip   = { member = true }
```

### Phase 4: Verilog/SystemVerilog Lexer

Files: `token.hpp`, `lexer.hpp`, `verilog_token.hpp`, `verilog_keywords.hpp`, `sv_token.hpp`, `sv_keywords.hpp` + implementations + tests

- [x] Implement `Token<T>` template with position tracking (file, line, col)
- [x] Implement character-level `Lexer` state machine
- [x] Define `VerilogTokenType` enum (~40 token types + SV extensions in single enum)
- [x] Implement Verilog keyword lookup table (~90 keywords including SV)
- [x] Implement Verilog tokenizer (identifiers, numbers, strings, comments, directives, operators)
- [x] Handle escaped identifiers
- [x] SV keywords in single enum, gated by `is_sv` flag in lexer
- [x] Implement SV keyword recognition (logic, interface, always_comb, etc.)
- [x] Implement SV tokenizer (same lexer with is_sv=true)
- [x] Preserve `///` doc comments in token stream (for Phase 14 documentation extraction)
- [x] Preserve `// loom: ignore[...]` suppression comments (for Phase 13 lint)
- [x] Write test fixtures (`simple_module.v`, `counter.v`, `package_example.sv`)
- [x] Write tests for both lexers (30 test cases)
- [x] Verify performance: < 100ms for 10K-line file (17ms actual)

### Phase 5: Verilog/SystemVerilog Parser and Design Unit Extraction

Files: `parser.hpp`, `ir.hpp`, `reference.hpp`, `design_unit.hpp`, `verilog_parser.cpp`, `sv_parser.cpp` + tests

Expanded from original to extract additional AST structures needed by the lint engine (Phase 13). The parser performs a single pass that collects design units, ports, parameters, instantiations, AND lightweight lint-relevant structures.

- [x] Implement `Parser` base with token stream navigation
- [x] Define `DesignUnit` struct (kind, name, ports, params, references)
- [x] Implement `CompoundIdentifier` and `RefSet`
- [x] Implement Verilog module/endmodule extraction
- [x] Implement port and parameter extraction (with type/width info for docs)
- [x] Implement instantiation detection heuristic (`IDENT IDENT (`)
- [x] Implement SV interface/package/class extraction
- [x] Implement `import` statement detection
- [x] Handle nested modules (depth tracking)
- [x] Implement error recovery (skip to next `;` or `endmodule`)
- [x] Extract lint-relevant structures during the same parse pass:
  - `AlwaysBlock` with type (always_comb, always_ff, always_latch, always @*)
  - `Assignment` with blocking/non-blocking flag and location
  - `CaseStatement` with kind, has_default, has_unique
  - `Instantiation` with port connections (detect empty `.port()`)
  - `SignalDecl` with is_assigned/is_read tracking (intra-module)
  - `GenerateBlock` with label presence
  - `LabeledBlock` with begin/end label matching
- [x] Produce `DesignUnit` struct that bundles all extracted data per module
- [x] Write tests and fixtures

**ModuleAST** (consumed by lint, docs, and filelist):
```cpp
struct ModuleAST {
    std::string name;
    int start_line, end_line;
    std::vector<SignalDecl> signals;
    std::vector<AlwaysBlock> always_blocks;
    std::vector<CaseStatement> case_statements;
    std::vector<Instantiation> instantiations;
    std::vector<GenerateBlock> generate_blocks;
    std::vector<LabeledBlock> labeled_blocks;
    std::vector<std::string> port_names;
};
```

### Phase 6: Graph Data Structures

Files: `graph.hpp` (header-only template) + tests

- [x] Implement `Graph<NodeData, EdgeData>` with adjacency list
- [x] Implement `add_node`, `add_edge`, `has_edge`
- [x] Implement `successors`, `predecessors`, `in_degree`, `out_degree`
- [x] Implement topological sort (Kahn's algorithm)
- [x] Implement minimal topological sort (from root)
- [x] Implement cycle detection
- [x] Implement DFS traversal
- [x] Implement tree display formatting
- [x] Implement `GraphMap` (string-keyed convenience wrapper)
- [x] Write tests (linear, diamond, cycle, disconnected, edge data, GraphMap)
- [x] Verify performance: < 50ms for 10K nodes (0-1ms actual)

### Phase 7: Git Dependencies and Cache Manager

Files: `git.hpp`, `source.hpp`, `cache.hpp` (git cache), `lockfile.hpp` (format definition) + implementations + tests

Replaces the original channel/archive/catalog system. All dependency sources are explicit (git URL or local path). Shells out to `git` CLI rather than using libgit2 (libgit2 lacks shallow clone support and has fragile SSH auth).

- [x] Implement `GitCli` class — subprocess wrapper for git operations:
  - `check_version()` — verify git is installed and meets minimum version
  - `ls_remote()` — list remote refs (tags, branches) without cloning
  - `clone_bare()` — clone bare repo into cache
  - `fetch()` — update existing bare repo
  - `checkout()` — create working tree from bare repo at specific commit
  - `shallow_clone_tag()` — fast checkout of a specific tag
  - `resolve_ref()` — resolve tag/branch/short-SHA to full commit SHA
  - `show_file()` — read a file from a commit without full checkout
  - `set_timeout()` / `set_offline()` — configuration
- [x] Implement `run_command()` helper — fork/exec with stdout/stderr capture and timeout
- [x] Implement semver tag parsing from `git ls-remote --tags` output:
  - Filter tags matching `vX.Y.Z[-prerelease]` pattern
  - Sort by semver ordering, select highest matching version
  - Dereference annotated tags to commit SHA
- [x] Implement `CacheManager` class — two-tier git cache:
  - `~/.loom/cache/git/db/<name>-<hash(url)>/` — bare repos (shared across versions)
  - `~/.loom/cache/git/checkouts/<name>-<hash(url)>/<version>-<short-sha>/` — working trees
  - `cache_dir_name()` — deterministic name from URL using SHA-256 hash (16 hex chars)
  - `ensure_bare_repo()` — clone or fetch
  - `ensure_checkout()` — create working tree at specific commit
  - `compute_checksum()` — SHA-256 hash of checked-out source tree
  - `clean_checkouts()` / `clean_all()` — cache cleanup
- [x] Define `LockedPackage` struct and `LockFile` struct (TOML format):
  - Fields: name, version, source (`git+<url>` or `path+<path>`), commit, tag/branch/rev, checksum, dependencies
  - `load()` / `save()` / `is_stale()` / `find()`
- [ ] Implement offline mode (`--offline` flag, `LOOM_OFFLINE=1` env var):
  - Resolution uses locally-cached bare repos only
  - `loom update` errors in offline mode
  - Automatic fallback when network unreachable + cache exists
- [x] Write tests: git CLI subprocess, tag parsing, cache directory naming, lockfile roundtrip

**Error codes used** (all already defined in `error.hpp`): `IO`, `Network`, `NotFound`, `Version`, `Dependency`, `Manifest`, `Checksum`, `Cycle`

### Phase 8: Incremental Build Cache (SQLite)

Files: `cache.hpp` (build cache), `cache.cpp` + tests. New dependency: `third_party/sqlite3/sqlite3.{h,c}`

Content-addressed caching system that avoids re-parsing unchanged files. Based on ccache's inode cache pattern (stat-based fast path) and Bazel's action cache (composite key hashing).

- [x] Add SQLite amalgamation to `third_party/sqlite3/` and CMakeLists.txt
- [x] Define SQLite schema (6 tables):
  - `schema_info` — version tracking for migrations
  - `file_stat` — stat cache: `(path, inode, mtime, size) -> content_hash`
  - `parse_result` — parse cache: `content_hash -> serialized ParseResult`
  - `include_dep` — include tracking: `source_hash -> (include_path, include_hash)`
  - `dep_edge` — dependency edges: `source_hash -> (source_unit, target_unit)`
  - `filelist` — filelist cache: `filelist_key -> (file_list, top_modules)`
- [x] Implement `BuildCache` class with pImpl pattern (SQLite handle isolation):
  - `open()` — open/create database with WAL mode, set PRAGMAs
  - `lookup_stat()` / `update_stat()` / `remove_stat()` — stat cache
  - `lookup_parse()` / `store_parse()` — parse result cache
  - `get_includes()` / `store_includes()` / `find_includers()` — include dep tracking
  - `get_edges()` / `store_edges()` — dependency edge tracking
  - `lookup_filelist()` / `store_filelist()` — filelist cache
  - `prune()` — remove orphaned entries
  - `clear()` — delete all data
- [x] Implement `ParseResult` binary serialization format:
  - Magic `"LPR\x01"`, followed by design unit data (kind, name, ports, params, instantiations, includes)
  - Compact binary encoding (~3-5x smaller and ~10x faster than JSON)
- [x] Implement layered hash computation:
  - Layer 1: `file_content_hash = SHA-256(file bytes)` with stat shortcut
  - Layer 2: `parse_cache_key = content_hash`
  - Layer 3: `effective_hash = SHA-256(content_hash + include_hashes + defines + include_dirs)`
  - Layer 4: `filelist_key = SHA-256(loom_version + manifest_hash + all effective_hashes)`
- [x] Implement cascading invalidation via reverse-dependency index (include_dep reverse lookup)
- [x] Implement `cached_file_hash()`, `compute_effective_hash()`, `compute_filelist_key()` helpers
- [x] Handle edge cases: concurrent access (WAL), corruption (recreate), symlinks (canonical path), Loom version upgrade (schema migration)
- [x] Write tests: stat cache hit/miss, parse result roundtrip, include invalidation cascade, filelist cache, schema migration, corruption recovery (25 tests, 199 assertions)

**Performance targets**: stat lookup < 0.1ms, parse lookup < 0.5ms, full incremental check (1000 files, 0 changed) < 200ms

### Phase 9: Workspace, Project Model, and Local Overrides

Files: `workspace.hpp`, `project.hpp`, `local_override.hpp` + implementations + tests

Combines workspace discovery, project model, and `Loom.local` override mechanism.

#### 9a: Workspace Support

Based on Cargo workspaces. Virtual and root-package workspace types. Single `Loom.lock` at root.

- [ ] Implement `WorkspaceMember` struct: name, version, manifest_path, root_dir, parsed manifest
- [ ] Implement `WorkspaceConfig` struct: shared dependencies, lint config, target configs
- [ ] Implement `Workspace` class:
  - `discover(start_dir)` — walk up from cwd, find `Loom.toml` with `[workspace]`
  - `load(workspace_root)` — load root manifest + expand member globs
  - `expand_member_globs()` — resolve `members = ["ip/*"]` patterns using loom::glob
  - `find_member()` — lookup by package name
  - `member_for_path()` — find member containing a given path
  - `resolve_targets()` — determine which members to build given CLI flags (-p, --all, cwd)
  - `resolve_workspace_dep()` — resolve `{ workspace = true }` from `[workspace.dependencies]`
  - `resolve_member_dep()` — resolve `{ member = true }` to sibling path dependency
  - `build_dependency_graph()` — unified graph for multiple members with conflict detection
  - `effective_config()` — merge workspace config with member overrides (member wins)
  - `validate()` — no duplicate names, no nested workspaces, no member-level lock files
- [ ] Implement `is_workspace_root()` and `find_manifest()` free functions

**Workspace Loom.toml syntax**:
```toml
[workspace]
members = ["ip/*", "soc/top"]
exclude = ["ip/deprecated_uart"]
default-members = ["soc/top"]

[workspace.dependencies]
common_cells = { git = "...", tag = "v1.37.0" }
```

#### 9b: Project Model

- [ ] Implement project detection (walk up for `Loom.toml`)
- [ ] Implement project loading and checksum
- [ ] Implement source file collection from `[[sources]]` groups with target filtering

#### 9c: Local Overrides (Loom.local)

Based on Bender.local and Cargo [patch]. Overrides bypass lockfile without modifying it.

- [ ] Define `OverrideSource` struct: `Kind::Path` or `Kind::Git` with url/branch/tag/rev
- [ ] Implement `LocalOverrides` struct:
  - `load(local_file)` — parse `Loom.local` TOML
  - `has_override()` / `get_override()` / `count()` / `empty()`
  - `validate()` — path exists and contains `Loom.toml`, git URL non-empty
  - `warn_active()` — emit warning banner via `loom::log::warn`
- [ ] Implement `discover_local_overrides(project_root)` — returns empty (not error) if no file
- [ ] Implement override application rules:
  - `loom lock`: Loom.local is IGNORED (warning printed), lockfile reflects Loom.toml only
  - `loom build`/`plan`/`tree`/`get`: overrides applied, replace locked sources
  - `--no-local` flag and `LOOM_NO_LOCAL=1` env var suppress overrides
  - Nested override's own `Loom.local` is silently ignored (only main project overrides apply)
  - Version mismatch between override and declared dep: warning, not error
- [ ] Add `Loom.local` to generated `.gitignore` in `loom init`/`loom new`
- [ ] Write tests: parsing, validation, warning output, lock interaction, override application

**Loom.local syntax**:
```toml
[overrides]
axi_crossbar = { path = "../axi/crossbar" }
common_cells = { git = "git@github.com:user/common_cells.git", branch = "fix-mux" }
```

### Phase 10: Dependency Resolution and Lockfile

Files: `resolver.hpp`, `resolver.cpp` + tests

- [ ] Implement `DependencyResolver` class:
  - `resolve(manifest)` — full resolution from manifest, producing lockfile
  - `update(manifest, existing_lock, package_name)` — selective re-resolution
  - `resolve_git()` — resolve a single git dependency to commit SHA
  - `resolve_version_constraint()` — find best matching semver tag
  - `discover_versions()` — list semver tags from remote
  - `load_remote_manifest()` — read `Loom.toml` from commit via `git show`
  - `topological_sort()` — order resolved packages, detect cycles
- [ ] Implement BFS resolution algorithm with constraint checking
- [ ] Implement conflict detection (two dependents requiring incompatible versions)
- [ ] Implement lockfile staleness detection (deps changed, source/constraint changed)
- [ ] Integrate with `LocalOverrides` — apply overrides after lockfile load
- [ ] Integrate with `Workspace` — unified resolution across workspace members
- [ ] Implement `ResolveContext` struct: manifest, lockfile, local overrides, workspace, no_local flag
- [ ] Write tests (single dep, transitive, diamond, conflict, cycle, workspace, local override)

### Phase 11: Filelist Generation (with Target Filtering)

Files: `filelist.hpp` + implementation + tests

Generates topologically-sorted file lists for EDA tools, now incorporating target expression filtering from Phase 2.

- [ ] Build unit-level graph from parsed design units
- [ ] Map units to files (file-level graph)
- [ ] Integrate target filtering into filelist pipeline:
  1. Parse `Loom.toml` → list of `SourceGroup` (each with optional `TargetExpr`)
  2. Resolve dependencies → full dep tree with all source groups
  3. Evaluate each `SourceGroup` against active target set → include or skip
  4. Parse included files → extract design units
  5. Build dependency graph → topological sort
  6. Emit filelist (ordered file list)
- [ ] Implement topological sort of file graph
- [ ] Implement output formats: standard file list (`.f`), JSON
- [ ] Implement top-level module detection
- [ ] Implement testbench heuristic
- [ ] Handle black box modules (warnings)
- [ ] Integrate with incremental build cache (Phase 8) — skip unchanged files
- [ ] Write tests

### Phase 12: EDA Tool Drivers

Files: `include/loom/target/types.hpp`, `tool_driver.hpp`, `driver_*.hpp` + implementations + tests

Built-in drivers for 9 EDA tools plus a custom driver for arbitrary tools. Three-phase lifecycle: `configure()` → `build()` → `run()`.

- [ ] Define core data structures in `types.hpp`:
  - `SourceFile` — path, language (Verilog/SV), is_include_file, library
  - `Filelist` — name, top_module, files, include_dirs, defines, parameters, plusargs
  - `ToolAction` enum — Lint, Simulate, Synthesize, Build
  - `ToolResult` — exit_code, stdout/stderr logs, work_dir, waveform/artifact paths
  - `ToolOptions` — compile/elaborate/simulate/synth args, timescale, waveform config, device/family
- [ ] Implement `ToolDriver` abstract base class:
  - Identity: `name()`, `display_name()`, `supported_actions()`, `supports()`
  - Discovery: `find_executable()`, `detect_version()`
  - Lifecycle: `configure()`, `build()`, `run()`, `execute()` (one-shot)
  - Helpers: `write_standard_filelist()`, `write_tcl_script()`, `run_command()`
- [ ] Implement `create_driver()` factory and `available_drivers()` registry
- [ ] Implement per-tool drivers:
  - `IcarusDriver` — iverilog/vvp, `.scr` command file, lint+simulate
  - `VerilatorDriver` — verilator, `.vc` file, lint+simulate, `--trace-fst`
  - `VivadoSimDriver` — xvlog/xelab/xsim, TCL batch, simulate
  - `VivadoSynthDriver` — vivado batch, `build.tcl`, synthesize+build
  - `QuartusDriver` — quartus_sh, TCL setup, synthesize+build
  - `ModelSimDriver` — vsim/vlog, TCL + DO files, simulate
  - `VcsDriver` — vcs/simv, `.f` file list, simulate
  - `XceliumDriver` — xrun (single invocation), `.f` file list, simulate
  - `YosysDriver` — yosys, `.ys` script, synthesize
  - `CustomDriver` — user-defined commands in Loom.toml with `{{ variable }}` substitution via Swap engine
- [ ] Implement target resolution rules:
  1. Explicit: `--target synth-vivado`
  2. Default by action: `loom build` → first synthesis target, `loom test` → first simulation target
  3. Auto-detection: probe PATH in priority order (verilator > icarus > xsim > modelsim > vcs > xcelium)
- [ ] Write tests (driver registry, file list generation, TCL generation, tool discovery mocking)

**Loom.toml target configuration**:
```toml
[targets.sim]
tool = "verilator"
action = "simulate"
[targets.sim.options]
compile_args = ["--timing", "-Wall"]
waveform = true
waveform_format = "fst"

[targets.synth-vivado]
tool = "vivado-synth"
action = "synthesize"
[targets.synth-vivado.options]
device = "xc7a35ticsg324-1L"
```

### Phase 13: Lint Engine

Files: `include/loom/lint/lint_rule.hpp`, `lint_engine.hpp`, `lint_config.hpp`, `lint_suppression.hpp`, `rules/*.hpp` + implementations + tests

22 lightweight lint rules in 3 categories (correctness, structure, style) that operate on the parser's `ModuleAST` and `TokenStream` without requiring full elaboration.

- [ ] Define `Severity` enum: `Off`, `Warn`, `Error`
- [ ] Define `Diagnostic` struct: location, severity, rule_id, message
- [ ] Define `LintRule` abstract base class: `id()`, `description()`, `category()`, `default_severity()`, `check()`
- [ ] Implement `LintConfig`: parse `[lint]` from Loom.toml, rule severity overrides, naming patterns
- [ ] Implement `SuppressionMap`: scan token stream for `// loom: ignore[rule-id]` comments
  - Next-line suppression: `// loom: ignore[rule-id]`
  - Same-line suppression
  - Range suppression: `// loom: ignore-start[rule-id]` ... `// loom: ignore-stop[rule-id]`
  - Wildcard: `// loom: ignore[*]`
- [ ] Implement `LintEngine`:
  - `configure()` — load config, set effective severities
  - `lint_file()` — lex + parse + run rules
  - `lint_parsed()` — run rules on pre-parsed data (avoid re-parsing in `loom build`)
  - `lint_project()` — lint all HDL files, produce `LintReport`
  - `apply_suppressions()` — filter diagnostics through suppression map
- [ ] Implement 22 lint rules:

  **Correctness (11 rules)**:
  - `blocking-in-ff` (error) — blocking `=` in `always_ff`
  - `nonblocking-in-comb` (error) — non-blocking `<=` in `always_comb`
  - `mixed-blocking` (error) — both `=` and `<=` in same always block
  - `case-missing-default` (warn) — case without default (unless `unique`)
  - `casex-usage` (warn) — prefer `casez`/`case...inside`
  - `implicit-net` (warn) — undeclared signal without `default_nettype none`
  - `empty-port-connection` (warn) — `.data()` with no expression
  - `missing-port-connection` (warn) — instance doesn't connect all ports
  - `duplicate-module` (error) — two modules with same name in project
  - `always-star` (warn) — `always @*` instead of `always_comb`
  - `defparam-usage` (warn) — deprecated `defparam`

  **Structure (8 rules)**:
  - `missing-begin-end` (warn) — multi-statement body without `begin`/`end`
  - `label-mismatch` (warn) — end label doesn't match begin label
  - `unlabeled-generate` (warn) — generate block without label
  - `ifdef-balance` (error) — unmatched `ifdef`/`endif`
  - `one-module-per-file` (off) — multiple modules in one file
  - `module-filename-match` (off) — module name vs filename mismatch
  - `unused-signal` (warn) — declared but never read (intra-module)
  - `undriven-signal` (warn) — read but never assigned (intra-module)

  **Style (3 rules)**:
  - `naming-module` (off) — configurable regex, default `[a-z][a-z0-9_]*`
  - `naming-signal` (off) — configurable regex
  - `naming-parameter` (off) — configurable regex, default `[A-Z][A-Z0-9_]*`

- [ ] Implement output format: `file:line:col: severity: [rule-id] message` (GCC-compatible)
- [ ] Implement JSON output format (`--format json`)
- [ ] Implement integration with `loom build` via `[build] pre-lint = true`
- [ ] Write tests for each rule + suppression + config overrides
- [ ] Verify performance: < 200ms for 50-file project

**Summary**: 15 rules enabled by default (11 warn, 4 error), 7 off by default (opt-in).

### Phase 14: Documentation Generation

Files: `include/loom/doc/doc_comment.hpp`, `doc_model.hpp`, `doc_extractor.hpp`, `renderer.hpp`, `markdown_renderer.hpp`, `html_renderer.hpp`, `template_engine.hpp` + implementations + tests

Generates module-level API documentation from `///` doc comments in HDL source files. No existing open-source tool combines parser-based extraction + doc comments + cross-references + diagrams for Verilog/SV.

- [ ] Define doc comment data structures:
  - `DocTag` — kind (Param, Port, See, Deprecated, WaveDrom, Example), name, text
  - `DocComment` — brief, body, tags; methods: `tags_of()`, `find_param_doc()`, `has_deprecated()`
- [ ] Implement `DocExtractor`:
  - Walk token stream, find `///` comment blocks
  - Associate comments with next design unit (leading) or current port/param (trailing)
  - Parse `@tag` directives
  - Auto-brief from first sentence if no explicit `@brief`
- [ ] Define `DocModel` intermediate representation:
  - `PortDoc` — name, direction, type/width, description (from `@port` or trailing `///`)
  - `ParamDoc` — name, type, default, description (from `@param` or trailing `///`)
  - `CrossRef` — target name, resolved/unresolved, link path
  - `DesignUnitDoc` — kind, name, source location, doc comment, ports, params, instantiations, instantiated_by
  - `DocModel` — package info, all unit docs; `resolve_cross_refs()`, `find_unit()`
- [ ] Implement `MarkdownRenderer`:
  - Index page with module listing
  - Per-unit pages: description, parameter table, port table, dependency graph, cross-refs
  - Mermaid dependency graphs (rendered natively by GitHub)
  - Search index JSON
- [ ] Implement `HtmlRenderer`:
  - Static HTML site with sidebar navigation, search, rendered diagrams
  - Built-in templates (compiled as string literals in `src/doc/templates/`)
  - Mermaid.js and WaveDrom.js included for client-side rendering
  - User-overridable templates in `.loom/doc-templates/`
- [ ] Implement `TemplateEngine` (built on Swap engine from Phase 1):
  - `{{ variable }}` substitution
  - `{{# each items }}` loops
  - `{{# if condition }}` conditionals
  - Load custom templates with fallback to built-in
- [ ] Write tests: doc comment parsing, extractor, model construction, cross-ref resolution, Markdown output, HTML output, template engine

**Doc comment syntax**:
```systemverilog
/// A parameterizable FIFO buffer.
///
/// @param DEPTH Number of entries (must be power of 2).
/// @see fifo_async
/// @deprecated Use fifo_v2 instead.
module fifo_sync #(
    parameter DEPTH = 16  /// Number of entries
)(
    input  logic clk,     /// System clock
    output logic full     /// FIFO full flag
);
```

### Phase 15: CLI Interface and Commands

Files: `cli.cpp`, `main.cpp`, `cmd_*.cpp`

- [ ] Implement CLI argument parser and command router
- [ ] Implement project commands:
  - `loom new <name>` — create new project with scaffold
  - `loom init` — initialize in existing directory
  - `loom init --workspace` — initialize workspace
  - `loom info` — show project/workspace info, active overrides
  - `loom env` — show environment info
  - `loom env --tools` — list available EDA tools and their versions
  - `loom config` — view/set configuration
- [ ] Implement dependency commands:
  - `loom lock` — resolve dependencies, write `Loom.lock` (ignores `Loom.local`)
  - `loom update [package]` — re-resolve all or specific dependency
  - `loom fetch` — pre-download all deps for offline work
  - `loom tree` — display dependency tree (annotates overridden sources)
  - `loom clean` — remove cache (`loom clean --all` removes `.loom/`)
- [ ] Implement build commands:
  - `loom build [--target <name>]` — generate filelist + invoke EDA tool driver
  - `loom test [--target <name>]` — simulation shortcut (equivalent to `loom build` with sim target)
  - `loom plan [--target <targets>] [-o <file>]` — generate filelist without executing
  - `loom build -p <member>` — build specific workspace member
  - `loom build --all` — build all workspace members
  - `loom build -- <extra-args>` — pass-through to EDA tool
  - `loom build --wave [--wave-format fst]` — enable waveform dumping
- [ ] Implement quality commands:
  - `loom lint [files...]` — run lint engine
  - `loom lint --rule <rule-id>` — run specific rule(s)
  - `loom lint --severity error` — filter by severity
  - `loom lint --format json` — JSON output for tooling
  - `loom lint --strict` — treat warnings as errors
  - `loom doc [--format html|md]` — generate documentation
  - `loom doc --open` — generate and open in browser
- [ ] Implement global flags:
  - `--no-local` — suppress `Loom.local` overrides
  - `--offline` — no network access
  - `--target <targets>` / `-t <targets>` — active target set (comma-separated)
  - `--verbose` / `-v` — increase log level
  - `--help` — per-command help
- [ ] Implement fuzzy command suggestions (Levenshtein distance)
- [ ] Write integration tests

### Phase 16: Integration, Symbol Remapping, and Polish

Files: `verilog_sr.cpp`, `sv_sr.cpp`, integration tests

- [ ] Implement symbol remapping collision detection
- [ ] Implement SHA-256-based name mangling
- [ ] Implement token-stream identifier replacement
- [ ] Create integration test workspace (multi-member, multi-dep)
- [ ] Run full end-to-end pipeline test: `loom new` → `loom lock` → `loom plan` → `loom build` → `loom lint` → `loom doc`
- [ ] Test `Loom.local` override workflow end-to-end
- [ ] Test workspace build workflow end-to-end
- [ ] Test incremental cache hit/miss behavior end-to-end
- [ ] Add progress indicators for long operations
- [ ] Add file locking (`flock()`) for concurrent safety
- [ ] Add signal handling (SIGINT cleanup)
- [ ] Run all tests under ASan + UBSan
- [ ] Final regression pass across all 16 phases

## Research Documents

Detailed specifications for each differentiation feature are in `docs/research/`:

| Feature | Reference |
|---------|-----------|
| EDA Tool Drivers | Agent output (a5c14fd): ToolDriver C++ spec, per-tool CLI patterns, Loom.toml target syntax |
| Target Expressions | Agent output (a9390ac): TargetExpr class, recursive descent parser, Bender grammar analysis |
| Local Overrides | Agent output (a1a58cf): Loom.local spec, interaction rules, Bender/Cargo/Go prior art |
| Git Dependencies | Agent output (acdbfa1): GitCli class, cache strategy, lockfile format, semver resolution |
| Incremental Cache | Agent output (afcbda9): SQLite schema, 4-layer hashing, invalidation algorithm, performance targets |
| Lint Engine | Agent output (a0586ab): 22 rules, LintRule base class, suppression syntax, Verilator/Verible analysis |
| Workspace Support | Agent output (a5ade01): Workspace class, member discovery, single lockfile, Cargo/pnpm analysis |
| Documentation | `docs/research/loom_doc_specification.md`: DocModel, renderers, template engine, Mermaid/WaveDrom |

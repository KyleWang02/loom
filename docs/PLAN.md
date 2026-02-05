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
- [ ] Phase 1: Foundation Layer (Result, SHA256, UUID, Glob, Swap, Log) ← CURRENT
- [ ] Phase 2: Manifest, Configuration, and Versioning ← NEXT UP
- [ ] Phase 3: Verilog/SystemVerilog Lexer and Tokenizer
- [ ] Phase 4: Verilog/SystemVerilog Parser and Design Unit Extraction
- [ ] Phase 5: Graph Data Structures and Algorithms
- [ ] Phase 6: Project Model, Catalog, and File System
- [ ] Phase 7: Dependency Resolution and Lockfile
- [ ] Phase 8: Blueprint Generation
- [ ] Phase 9: Archive, Checksum, and Distribution
- [ ] Phase 10: CLI Interface and Command Router
- [ ] Phase 11: Targets, Protocols, and Extensibility
- [ ] Phase 12: Integration, DST, and Polish

## Phase Dependency Map

```
Phase 1 (Foundation)
  ├──> Phase 2 (Manifest, Config, Version) ──┐
  ├──> Phase 3 (Lexer) ──┐                   |
  ├──> Phase 5 (Graph) ──┤                   |
  |                       v                   |
  |               Phase 4 (Parser)            |
  |                       |                   v
  |                       └──> Phase 6 (Project, Catalog)
  |                                  |
  |                                  v
  |                          Phase 7 (Dep Resolution)
  |                                  |
  |                                  v
  |                          Phase 8 (Blueprint)
  |                                  |
  |                                  v
  |                          Phase 9 (Archive)
  |                                  |
  |                                  v
  |                          Phase 10 (CLI)
  |                                  |
  |                                  v
  |                          Phase 11 (Targets, Protocols)
  |                                  |
  |                                  v
  └──────────────────────> Phase 12 (Integration, DST)
```

Parallelizable: Phases 2, 3, and 5 can be done in parallel (all depend only on Phase 1).

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
- [ ] Implement UUID v4 generation + base36 encoding/decoding
- [ ] Implement glob pattern matching (`*`, `**`, `?`, `[a-z]`, negation)
- [ ] Implement `{{ variable }}` swap/substitution engine
- [x] Set up Catch2 integration in CMake
- [ ] Write tests for all components
- [x] Verify all NIST SHA-256 test vectors pass
- [ ] Verify UUID base36 roundtrip is lossless
- [x] Run under AddressSanitizer

### Phase 2: Manifest, Configuration, and Versioning

Files: `version.hpp`, `name.hpp`, `manifest.hpp`, `config.hpp`, `source.hpp` + implementations + tests

- [ ] Implement `Version` (major.minor.micro + optional label) with parsing and comparison
- [ ] Implement `PartialVersion` with compatibility checking
- [ ] Implement `PkgName` with validation and normalization
- [ ] Implement `Manifest` (Loom.toml) parsing with toml++
- [ ] Implement `Dependency` (string shorthand + table format)
- [ ] Implement `Config` with layered loading (global > regional > local)
- [ ] Implement config merge rules
- [ ] Write test fixtures (`Loom.toml.example`, `config.toml.example`)
- [ ] Write tests for all components
- [ ] Verify manifest roundtrip (load -> save -> load)

### Phase 3: Verilog/SystemVerilog Lexer

Files: `token.hpp`, `lexer.hpp`, `verilog_token.hpp`, `verilog_keywords.hpp`, `sv_token.hpp`, `sv_keywords.hpp` + implementations + tests

- [ ] Implement `Token<T>` template with position tracking
- [ ] Implement character-level `Lexer` state machine
- [ ] Define `VerilogTokenType` enum (~40 token types)
- [ ] Implement Verilog keyword lookup table (~100 keywords)
- [ ] Implement Verilog tokenizer (identifiers, numbers, strings, comments, directives, operators)
- [ ] Handle escaped identifiers
- [ ] Define `SvTokenType` (extends Verilog)
- [ ] Implement SV keyword table (adds ~130 keywords)
- [ ] Implement SV tokenizer
- [ ] Write test fixtures (`simple_module.v`, `counter.v`, `package_example.sv`)
- [ ] Write tests for both lexers
- [ ] Verify performance: < 100ms for 10K-line file

### Phase 4: Parser and Design Unit Extraction

Files: `parser.hpp`, `ir.hpp`, `reference.hpp`, `design_unit.hpp`, `verilog_parser.cpp`, `sv_parser.cpp` + tests

- [ ] Implement `Parser` base with token stream navigation
- [ ] Define `DesignUnit` struct (kind, name, ports, params, references)
- [ ] Implement `CompoundIdentifier` and `RefSet`
- [ ] Implement Verilog module/endmodule extraction
- [ ] Implement port and parameter extraction
- [ ] Implement instantiation detection heuristic (`IDENT IDENT (`)
- [ ] Implement SV interface/package/class extraction
- [ ] Implement `import` statement detection
- [ ] Handle nested modules (depth tracking)
- [ ] Implement error recovery (skip to next `;` or `endmodule`)
- [ ] Write tests and fixtures

### Phase 5: Graph Data Structures

Files: `graph.hpp` (header-only template) + tests

- [ ] Implement `Graph<NodeData, EdgeData>` with adjacency list
- [ ] Implement `add_node`, `add_edge`, `has_edge`
- [ ] Implement `successors`, `predecessors`, `in_degree`, `out_degree`
- [ ] Implement topological sort (Kahn's algorithm)
- [ ] Implement minimal topological sort (from root)
- [ ] Implement cycle detection
- [ ] Implement DFS traversal
- [ ] Implement tree display formatting
- [ ] Implement `GraphMap` (string-keyed convenience wrapper)
- [ ] Write tests (linear, diamond, cycle, large graph)
- [ ] Verify performance: < 50ms for 10K nodes

### Phase 6: Project Model, Catalog, File System

Files: `project.hpp`, `fileset.hpp`, `visibility.hpp`, `catalog.hpp` + implementations + tests

- [ ] Implement project detection (walk up for `Loom.toml`)
- [ ] Implement project loading and checksum
- [ ] Implement fileset grouping (VLOG, SYSV, custom globs)
- [ ] Implement gitignore-compatible visibility rules
- [ ] Implement three-tier catalog (channels, archive, cache)
- [ ] Implement catalog scanning and indexing
- [ ] Implement `find_installed`, `find_highest_compatible`, `search`
- [ ] Write tests

### Phase 7: Dependency Resolution and Lockfile

Files: `lockfile.hpp`, `algo.cpp`, `context.hpp` + tests

- [ ] Implement BFS dependency resolution algorithm
- [ ] Implement conflict detection
- [ ] Implement `Loom.lock` parsing and writing
- [ ] Implement lockfile staleness detection
- [ ] Implement runtime `Context` struct
- [ ] Write tests (single dep, transitive, diamond, conflict, cycle)

### Phase 8: Blueprint Generation

Files: `blueprint.hpp` + implementation + tests

- [ ] Build unit-level graph from parsed design units
- [ ] Map units to files (file-level graph)
- [ ] Implement topological sort of file graph
- [ ] Implement TSV and JSON output formats
- [ ] Implement top-level module detection
- [ ] Implement testbench heuristic
- [ ] Handle black box modules (warnings)
- [ ] Write tests

### Phase 9: Archive and Distribution

Files: `archive.hpp` + implementation + tests

- [ ] Implement archive format (magic, header, zip body)
- [ ] Implement create/extract with miniz
- [ ] Implement header-only reading
- [ ] Implement directory checksum
- [ ] Implement cache slot naming
- [ ] Write tests (roundtrip, corruption detection)

### Phase 10: CLI and Commands

Files: `cli.cpp`, `main.cpp`, `cmd_*.cpp` (17 commands)

- [ ] Implement CLI argument parser and command router
- [ ] Implement `loom new` and `loom init`
- [ ] Implement `loom info` and `loom env`
- [ ] Implement `loom config`
- [ ] Implement `loom lock`
- [ ] Implement `loom tree`
- [ ] Implement `loom plan` (blueprint)
- [ ] Implement `loom build` and `loom test`
- [ ] Implement `loom get`
- [ ] Implement `loom install`, `loom search`, `loom publish`
- [ ] Implement `loom read`, `loom doc`, `loom remove`
- [ ] Implement `--help` for all commands
- [ ] Implement fuzzy command suggestions
- [ ] Write integration tests

### Phase 11: Targets, Protocols, Channels

Files: `target.hpp`, `protocol.hpp`, `channel.hpp` + implementations

- [ ] Implement target definition and execution
- [ ] Implement subprocess management (fork/exec, pipe capture)
- [ ] Implement protocol URL pattern matching
- [ ] Implement channel sync and hooks
- [ ] Write tests

### Phase 12: Integration, DST, Polish

Files: `verilog_dst.cpp`, `sv_dst.cpp`, integration tests

- [ ] Implement DST collision detection
- [ ] Implement SHA-256-based name mangling
- [ ] Implement token-stream identifier replacement
- [ ] Create integration test workspace
- [ ] Run full end-to-end pipeline test
- [ ] Add colored output with `isatty()` detection
- [ ] Add progress indicators
- [ ] Add fuzzy command suggestions (Levenshtein)
- [ ] Add file locking (`flock()`)
- [ ] Add signal handling (SIGINT cleanup)
- [ ] Run all tests under ASan + UBSan
- [ ] Final regression pass

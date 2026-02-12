# Session Log

<!-- This file contains ONLY the most recent session. -->
<!-- Previous sessions are in docs/ARCHIVE.md (cold storage). -->

## 2026-02-08 (Session 12)

**Focus**: Phase 15 (CLI Interface) + Phase 16 (Integration, Symbol Remapping, Polish) — ALL PHASES COMPLETE

**Completed**:
- **Phase 15 — CLI Interface and Commands**:
  - `include/loom/cli.hpp` — CLI framework: Flag, CliArgs, Command, CliParser, levenshtein()
  - `src/cli/cli.cpp` — Two-phase argument parsing, fuzzy command suggestions, grouped help (~440 lines)
  - 13 command files: cmd_new, cmd_init, cmd_info, cmd_env, cmd_config, cmd_lock, cmd_update, cmd_tree, cmd_clean, cmd_build, cmd_plan, cmd_lint, cmd_doc
  - `src/main.cpp` — Entry point with 5 global flags + 15 commands (including "test" alias and "fetch")
  - 25 test cases, 62 assertions — all passing
  - Verified: `loom --help`, `loom --version`, `loom buld` (suggests "build"), `loom new`, `loom info`, `loom lint`
- **Phase 16 — Integration, Symbol Remapping, and Polish**:
  - **Symbol Remapping**: `include/loom/sr.hpp` + `src/lang/verilog_sr.cpp`
    - SymbolRemapper: detect_collisions(), mangle(), build_remap_table(), remap_tokens(), remap_parse_result()
    - Collision detection scans top-level (depth==0) design units across all parsed files
    - Name mangling: `name + "_" + sha256(file_path + ":" + name)[0:8]`
    - Token remapping: replaces only Identifier tokens, ignores keywords/strings/numbers
    - Reports: format_report() (GCC-style), format_json()
    - 27 test cases, 75 assertions
  - **Polish Utilities**: `include/loom/util.hpp` + `src/util/util.cpp`
    - FileLock: RAII flock() wrapper, non-blocking first then blocking fallback
    - Progress: TTY-aware stderr progress indicator with `\r` line rewriting
    - Signal handling: std::atomic<bool> g_interrupted via sigaction for SIGINT/SIGTERM
  - **Integration Tests**: `tests/test_integration.cpp`
    - 15 end-to-end test cases covering: project discover, workspace discover+resolve, filelist pipeline, target filtering, build cache roundtrip, lint engine on real files, doc extractor, SR collision detection, local overrides, lockfile roundtrip, file locking, signal handling
    - 80 assertions — all passing
  - **ASan + UBSan**: All 33 tests pass clean under sanitizers
  - **Final regression**: 33 test executables, 1455+ assertions, 100% pass rate

**Key Decisions**:
- Two-phase CLI parsing: scan for subcommand boundary first, then parse global flags and command flags separately
- SR uses per-file remap tables (FileRemapTable) so each file defining a colliding name gets its own unique mangled name
- Integration tests use TempDir with write_file() pattern (no git repos needed, unlike test_resolver)
- FilelistOptions.include_testbenches defaults to false — modules with "tb" in name get filtered by default

**Issues & Fixes**:
- CLI `parse_flags()` consumed subcommand names as positionals — fixed with two-phase parsing
- `levenshtein("build", "biuld")` = 2 (not 1), transposition costs 2 in standard Levenshtein
- Repeatable flag double-counting: set_flag() already increments counts_, so don't also call increment()
- Integration test used wrong Manifest field names (package_name → package.name) and parse() signature
- Testbench filtering: tb_top excluded by default heuristic, fixed test to set include_testbenches=true

**Checkpoint Status**: ALL 16 PHASES COMPLETE. Project is feature-complete.

**Next**:
1. HTML renderer for documentation (deferred from Phase 14)
2. Template engine with loops/conditionals (deferred from Phase 14)
3. Offline mode implementation (deferred from Phase 7)
4. Port connection lint rules (empty-port-connection, missing-port-connection, missing-begin-end — deferred stubs)
5. Real-world testing on actual Verilog/SV projects

**Files Changed**:
- `include/loom/cli.hpp` (new — CLI framework header)
- `src/cli/cli.cpp` (new — CLI framework implementation, ~440 lines)
- `src/cli/cmd_new.cpp` (new)
- `src/cli/cmd_init.cpp` (new)
- `src/cli/cmd_info.cpp` (new)
- `src/cli/cmd_env.cpp` (new)
- `src/cli/cmd_config.cpp` (new)
- `src/cli/cmd_lock.cpp` (new)
- `src/cli/cmd_update.cpp` (new — registers both "update" and "fetch")
- `src/cli/cmd_tree.cpp` (new)
- `src/cli/cmd_clean.cpp` (new)
- `src/cli/cmd_build.cpp` (new — registers both "build" and "test")
- `src/cli/cmd_plan.cpp` (new)
- `src/cli/cmd_lint.cpp` (new)
- `src/cli/cmd_doc.cpp` (new)
- `src/main.cpp` (new — entry point)
- `tests/test_cli.cpp` (new — 25 cases, 62 assertions)
- `include/loom/sr.hpp` (new — Symbol Remapping header)
- `src/lang/verilog_sr.cpp` (new — SR implementation)
- `tests/test_sr.cpp` (new — 27 cases, 75 assertions)
- `include/loom/util.hpp` (new — FileLock, Progress, signal handling)
- `src/util/util.cpp` (new — polish utilities implementation)
- `tests/test_integration.cpp` (new — 15 cases, 80 assertions)
- `CMakeLists.txt` (modified — added all new source files and test targets)
- `docs/PLAN.md` (modified — all 16 phases marked complete)
- `CLAUDE.md` (modified — updated current state)

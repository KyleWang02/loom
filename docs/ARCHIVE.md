# Session Archive

<!-- Newest sessions at top. Only read this file when explicitly asked. -->
<!-- This is cold storage. It costs zero tokens unless you are told to check it. -->

---

## 2026-02-05 (Session 3)

**Focus**: Migrate session continuity workflow to V2 (tiered memory model)

**Completed**:
- Created `docs/ARCHIVE.md` — cold storage with all prior session entries
- Created `docs/ARCHITECTURE.md` — module map, dependency graph, key patterns
- Rewrote `docs/SESSION_LOG.md` — now single-session only (this file)
- Rewrote `CLAUDE.md` — lean launchpad under 1,500 tokens
- Rewrote `.claude/commands/handoff.md` — V2 workflow (archive → overwrite → update)
- Verified `docs/PLAN.md` markers are correct

**Key Decisions**:
- SESSION_LOG.md is now overwritten each handoff (not appended)
- Old entries go to ARCHIVE.md (prepended, newest first)
- ARCHITECTURE.md is updated only when structure changes, not every session
- CLAUDE.md trimmed to pointers + conventions + state (no architecture details)

**Checkpoint Status**: Mid Phase 1. Chunks 1-2 done (Error/Result/Log, SHA-256). Next: UUID, Glob, Swap.

**Next**:
1. Implement UUID v4 + base36 (`uuid.hpp` + `uuid.cpp`)
2. Implement glob pattern matching (`glob.hpp` + `glob.cpp`)
3. Implement swap engine (`swap.hpp` + `swap.cpp`)
4. Tests for each, following checkpoint workflow
5. Complete Phase 1, full regression, ASan pass

**Files Changed**:
- `docs/ARCHIVE.md` (new)
- `docs/ARCHITECTURE.md` (new)
- `docs/SESSION_LOG.md` (rewritten)
- `CLAUDE.md` (rewritten)
- `.claude/commands/handoff.md` (rewritten)

---

## 2026-02-04 (Session 2)

**Focus**: Phase 0 completion + Phase 1 foundation layer (first 2 chunks: Error/Result/Log, SHA-256)

**Completed**:
- Finished Phase 0: all scaffolding, docs, directory structure in place
- Added checkpoint workflow to `CLAUDE.md` and `docs/PLAN.md`
- Updated `.claude/commands/handoff.md` with checkpoint status field
- Downloaded Catch2 v2.13.10 single-header to `third_party/catch2/`
- Implemented `LoomError` struct (`error.hpp` + `error.cpp`)
- Implemented `Result<T>` template with monadic ops and `LOOM_TRY` macro (`result.hpp`)
- Implemented logging system with 5 levels and ANSI color (`log.hpp` + `log.cpp`)
- Implemented SHA-256 from NIST FIPS 180-4 (`sha256.hpp` + `sha256.cpp`)
- Created `demos/demo_errors.cpp` — real-world error pipeline demo with 4 scenarios
- Created `tests/fixtures/simple_module.v` — sample Verilog counter module
- Wrote and passed 21 tests for Result/Error, 7 for Log, 10 for SHA-256 (38 total, 94 assertions)
- All tests pass under AddressSanitizer + UndefinedBehaviorSanitizer
- Set up CMake `loom_add_test()` helper with `LOOM_SOURCE_DIR` env for fixture paths

**Key Decisions**:
- `Result<T>` has implicit constructor from `LoomError` so `LOOM_TRY` can propagate errors across different `Result<T>` return types
- Logging goes to stderr (not stdout), using C-style `va_list` for printf formatting
- Color auto-detected via `isatty(fileno(stderr))`, overridable with `set_color_enabled()`
- SHA-256 uses explicit byte manipulation (no `reinterpret_cast` for endianness)
- Test fixtures located via `LOOM_SOURCE_DIR` env var set by CMake `set_tests_properties`

**Issues & Gotchas**:
- `LOOM_TRY` originally failed to compile because it tried to return a bare `LoomError` from a function returning `Result<int>`. Fixed by making `Result<T>(LoomError)` constructor implicit.
- `tests/fixtures/` directory was missing (replaced by `tests/verilog_tests/` at some point). Recreated and copied file.
- Hand-written hex strings for `bytes_to_hex` test had typo (`099` instead of `99`). Always generate expected values programmatically.
- Test executables run from `build/` dir, so relative paths to fixtures break. Solved with `LOOM_SOURCE_DIR` env var.

**Failed Approaches**:
- First attempt at `LOOM_TRY` used `decltype(expr)::err(...)` to wrap the error. This fails when the expression type differs from the calling function's return type (e.g., `LOOM_TRY` on `Result<string>` inside a function returning `Result<int>`). The implicit constructor approach is cleaner and more general.

**Checkpoint Status**: User confirmed SHA-256 tests pass. Awaiting user confirmation to proceed to Phase 1 Chunk 3 (UUID v4 + base36).

**Next Session Should**:
1. Implement UUID v4 generation + base36 encoding/decoding (`uuid.hpp` + `uuid.cpp`)
2. Implement glob pattern matching (`glob.hpp` + `glob.cpp`)
3. Implement swap/substitution engine (`swap.hpp` + `swap.cpp`)
4. Write and run tests for each (following checkpoint workflow)
5. Complete Phase 1, do full regression, ASan pass
6. Get user sign-off on Phase 1 before moving to Phase 2

**Files Changed**:
- `CLAUDE.md` (updated — added checkpoint workflow section)
- `docs/PLAN.md` (updated — added checkpoint workflow summary, checkboxes updated)
- `docs/SESSION_LOG.md` (updated — this entry)
- `.claude/commands/handoff.md` (updated — added checkpoint status field)
- `CMakeLists.txt` (updated — added loom_core lib, test targets, demo target, LOOM_SOURCE_DIR)
- `include/loom/error.hpp` (new)
- `include/loom/result.hpp` (new)
- `src/util/error.cpp` (new)
- `include/loom/log.hpp` (new)
- `src/util/log.cpp` (new)
- `include/loom/sha256.hpp` (new)
- `src/util/sha256.cpp` (new)
- `tests/test_main.cpp` (new)
- `tests/test_result.cpp` (new)
- `tests/test_log.cpp` (new)
- `tests/test_sha256.cpp` (new)
- `demos/demo_errors.cpp` (new)
- `tests/fixtures/simple_module.v` (new)
- `third_party/catch2/catch.hpp` (new — downloaded)

---

## 2026-02-04 (Session 1)

**Focus**: Phase 0 — Project scaffolding and session management

**Completed**:
- Initialized git repository
- Created full directory structure
- Created `CLAUDE.md` with project summary, rules, architecture overview, naming conventions
- Created `docs/PLAN.md` with full phased roadmap and dependency map
- Created `docs/SESSION_LOG.md` (this file)
- Created `.claude/commands/handoff.md` slash command
- Created `.gitignore` and `CMakeLists.txt` skeleton

**Key Decisions**:
- Using `master` as default branch (git default)
- `CLAUDE.md` kept under ~2000 words for fast context loading
- `PLAN.md` uses checkbox tracking with `← CURRENT` / `← NEXT UP` markers
- Session log uses reverse chronological order (newest first)

**Issues & Gotchas**:
- None (initial setup)

**Failed Approaches**:
- None (initial setup)

**Next Session Should**:
1. Begin Phase 1: Foundation Layer
2. Set up Catch2 in `third_party/`
3. Implement `Result<T, LoomError>` and `LoomError`
4. Implement SHA-256 from NIST spec
5. Implement UUID v4 + base36
6. Implement glob matcher
7. Implement swap engine
8. Implement logging
9. Write tests for all Phase 1 components

**Files Changed**:
- `CLAUDE.md` (new)
- `docs/PLAN.md` (new)
- `docs/SESSION_LOG.md` (new)
- `.claude/commands/handoff.md` (new)
- `.gitignore` (new)
- `CMakeLists.txt` (new)

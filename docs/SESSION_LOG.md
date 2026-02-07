# Session Log

<!-- This file contains ONLY the most recent session. -->
<!-- Previous sessions are in docs/ARCHIVE.md (cold storage). -->

## 2026-02-07 (Session 8)

**Focus**: Phase 8 — Incremental Build Cache (SQLite)

**Completed**:
- **Phase 8 — Incremental Build Cache**: Full SQLite-backed build cache implementation:
  - Downloaded SQLite amalgamation v3.45.0 to `third_party/sqlite3/`
  - `build_cache.hpp` — `BuildCache` class with pImpl pattern (sqlite3.h never leaks into public header)
  - `build_cache.cpp` — ~660 lines: schema init, stat cache, binary serialization, parse cache, include/edge tracking, filelist cache, hash computation helpers, maintenance operations
  - 6 SQLite tables: `schema_info`, `file_stat`, `parse_result`, `include_dep`, `dep_edge`, `filelist`
  - Custom binary serialization with `LPR\x01` magic, varint encoding (~3-5x smaller than JSON)
  - 14 prepared statements cached for performance
  - WAL mode + synchronous=NORMAL for concurrent reader safety
  - Stat-based fast path: POSIX `stat()` checks inode/mtime/size before falling back to SHA256
  - Corruption recovery: detects corrupt DB on open, deletes and recreates automatically
  - Schema migration: version check on open, clears data if version mismatches
  - `compute_effective_hash()` and `compute_filelist_key()` for 4-layer hash strategy
  - 25 test cases (199 assertions), all passing, ASan/UBSan clean
- All 21 test executables pass (275+ total assertions including new build cache tests)

**Key Decisions**:
- Named `BuildCache` (not `Cache`) to avoid conflict with existing `CacheManager` in `cache.hpp` (git cache)
- pImpl pattern keeps `sqlite3.h` out of public header — users never see SQLite types
- SourcePos.file skipped in serialization (redundant — all units share the same file from cache key context)
- Filelist stored as comma-separated strings (no JSON dependency needed)
- Schema migration strategy: clear + re-init (cache is ephemeral, data loss is acceptable)

**Issues & Fixes**:
- `SCHEMA_VERSION` string concatenation inside `LOOM_TRY` macro caused preprocessor confusion → built SQL string before calling `LOOM_TRY`
- `sqlite3_open` succeeds even on corrupt files (corruption only detected on use) → unified PRAGMA + schema init into single lambda, recovery on any failure
- Test needed `<unistd.h>` for `getpid()` and `<sqlite3.h>` for schema migration test

**Checkpoint Status**: Phases 0-8 complete. All tests pass under ASan/UBSan.

**Next**:
1. Phase 9: Workspace, Project Model, and Local Overrides (depends on Phases 3+7, both done)
2. Phase 10: Dependency Resolution and Lockfile (depends on Phases 7+9)
3. Phase 11: Filelist Generation with Target Filtering (depends on Phases 5+6+10)
4. Phase 13: Lint Engine (depends on Phase 5, which is done)
5. Phase 14: Documentation Generation (depends on Phase 5, which is done)

**Files Changed**:
- `third_party/sqlite3/sqlite3.h` (new — SQLite v3.45.0 amalgamation header)
- `third_party/sqlite3/sqlite3.c` (new — SQLite v3.45.0 amalgamation source)
- `include/loom/build_cache.hpp` (new — BuildCache class with pImpl, 97 lines)
- `src/util/build_cache.cpp` (new — full implementation, ~660 lines)
- `tests/test_build_cache.cpp` (new — 25 test cases, ~820 lines)
- `CMakeLists.txt` (modified — added C language, sqlite3 static lib, build_cache.cpp, test target)

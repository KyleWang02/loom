# Session Log

<!-- This file contains ONLY the most recent session. -->
<!-- Previous sessions are in docs/ARCHIVE.md (cold storage). -->

## 2026-02-05 (Session 4)

**Focus**: Phase 1 Chunk 3 — UUID v4 + Base36 implementation and tests

**Completed**:
- Implemented `Uuid` struct with v4 generation, to_string, from_string, encode_base36, decode_base36 (`uuid.hpp` + `uuid.cpp`)
- RNG uses `/dev/urandom` with `std::mt19937_64` fallback
- Base36 uses big-endian 128-bit arithmetic (divide-by-36 for encode, multiply-by-36 for decode)
- Fixed-width 25-char base36 output with leading zero padding
- Added `uuid.cpp` to `loom_core` library and `test_uuid` target in CMakeLists.txt
- Wrote 17 test cases (155 assertions), all passing
- Full regression: 4/4 test suites pass (test_result, test_log, test_sha256, test_uuid)

**Key Decisions**:
- Base36 alphabet is `0-9a-z` (lowercase only for output, case-insensitive on input)
- from_string accepts both upper and lowercase hex
- decode_base36 accepts both upper and lowercase
- Removed unused `is_zero()` helper to eliminate compiler warning

**Checkpoint Status**: Chunk 3 (UUID) done — implementation and tests reviewed. Chunk 4 (Glob) implementation not yet started. User requested detailed explanation of UUID and next steps before proceeding.

**Next**:
1. Implement glob pattern matching (`glob.hpp` + `glob.cpp`)
2. Write glob tests (~18 cases)
3. Implement swap engine (`swap.hpp` + `swap.cpp`)
4. Write swap tests (~16 cases)
5. Complete Phase 1: full regression, ASan pass, update docs

**Files Changed**:
- `include/loom/uuid.hpp` (new)
- `src/util/uuid.cpp` (new)
- `tests/test_uuid.cpp` (new)
- `CMakeLists.txt` (modified — added uuid.cpp to loom_core, added test_uuid target)

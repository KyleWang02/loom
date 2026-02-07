# Loom - Verilog/SystemVerilog Package Manager

## What is Loom?

Loom is a package manager and build system for Verilog and SystemVerilog, written in C++17. It manages reusable HDL IP cores across projects, resolves transitive dependencies, generates topologically-sorted file lists (Filelists) for EDA tools, handles namespace collisions via Symbol Remapping (SR), and integrates with arbitrary EDA backends via configurable Targets.

Always make use of subagent workflows

**Language**: C++17 only. No VHDL support.
**Supported HDL**: Verilog (.v, .vl, .vlg) and SystemVerilog (.sv)

## Reference Files (read on demand, not always)

| File | When to read |
|------|-------------|
| `docs/PLAN.md` | Deciding what to work on next |
| `docs/ARCHITECTURE.md` | Working on unfamiliar modules, adding new components |
| `docs/SESSION_LOG.md` | Start of session — last session context only |
| `docs/ARCHIVE.md` | Only if explicitly asked to check past sessions |

## Conventions

- **Branding**: `Loom.toml`, `Loom.lock`, `.loom/`, `loom` CLI
- **No references** to the inspiration project in code, docs, or comments
- **Types**: `PascalCase` — **Functions**: `snake_case` — **Files**: `snake_case.hpp/.cpp`
- **Namespaces**: `loom`, `loom::log`, `loom::str`
- **Errors**: `Result<T, LoomError>` with `LOOM_TRY(expr)` macro, never exceptions
- **Memory**: RAII + `std::unique_ptr`, no raw `new`/`delete`
- **Tests**: Catch2 single-header. Fixtures via `LOOM_SOURCE_DIR` env var.
- **Build**: CMake 3.16+. `cmake --build build && ctest --test-dir build --output-on-failure`

## Checkpoint Workflow

Each phase uses a strict implement → test → review loop:

1. **Implement** a testable chunk → **STOP** → present test plan to user
2. User approves → **write & run tests** → fix failures
3. **STOP** → present review (results, run commands) → user runs tests
4. User confirms → proceed to next chunk or phase
5. Between phases: all tests green, user confirms, run `/handoff`

**Nothing proceeds without user approval.**

## Handoff

Run the `/handoff` command at end of session. It:
1. Prepends current `SESSION_LOG.md` into `ARCHIVE.md`
2. Overwrites `SESSION_LOG.md` with this session's entry
3. Updates `PLAN.md` checkboxes and markers
4. Updates "Current State" below
5. Updates `ARCHITECTURE.md` if structure changed

## Current State

- **Date**: 2026-02-06
- **Phase**: Phase 4 — Lexer COMPLETE. Ready for Phase 5/6+.
- **Branch**: main
- **Blockers**: None
- **Tests**: 223 passing across 13 suites
- **Terminology**: "Filelist" (not Blueprint), "Symbol Remapping/SR" (not DST)

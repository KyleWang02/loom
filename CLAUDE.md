# Loom - Verilog/SystemVerilog Package Manager

## What is Loom?

Loom is a package manager and build system for Verilog and SystemVerilog, written in C++17. It manages reusable HDL IP cores across projects, resolves transitive dependencies, generates topologically-sorted file lists (Blueprints) for EDA tools, handles namespace collisions via Dynamic Symbol Transformation (DST), and integrates with arbitrary EDA backends via configurable Targets.

**Language**: C++17 only. No VHDL support.
**Supported HDL**: Verilog (.v, .vl, .vlg) and SystemVerilog (.sv)

## Key Rules

- All branding uses "Loom": `Loom.toml`, `Loom.lock`, `.loom/`, `loom` CLI
- No references to the inspiration project (Orbit) in code, docs, or comments
- C++17 standard throughout; no raw `new`/`delete` — use RAII + `std::unique_ptr`
- Error handling via `Result<T, LoomError>` type (not exceptions)
- Logging via `loom::log` namespace with ANSI color support
- String utilities in `loom::str` namespace
- Testing with Catch2 (single-header)
- Build system: CMake (minimum 3.16)

## Architecture Overview

```
include/loom/     — Public headers
  lang/           — HDL language support (lexer, parser, DST)
    verilog/      — Verilog-specific
    sv/           — SystemVerilog-specific
src/
  util/           — Foundation utilities (error, log, sha256, uuid, glob, swap)
  core/           — Core domain (version, manifest, config, project, catalog, graph, blueprint)
  lang/           — Language implementation
  commands/       — CLI command implementations
tests/            — Catch2 test files
  fixtures/       — Test data files
third_party/      — Header-only dependencies (toml++, nlohmann/json, miniz, catch2)
```

## Error Handling Pattern

Every fallible function returns `Result<T>`. Use `LOOM_TRY(expr)` macro to propagate errors:

```cpp
Result<Manifest> load_project() {
    auto config = Config::load_layered();
    LOOM_TRY(config);
    // use config.value()...
}
```

## Naming Conventions

- Types: `PascalCase` (e.g., `DesignUnit`, `PkgName`)
- Functions/methods: `snake_case` (e.g., `find_highest_compatible`)
- Constants/enums: `PascalCase` values (e.g., `LoomError::Code::NotFound`)
- Files: `snake_case.hpp` / `snake_case.cpp`
- Namespaces: `loom`, `loom::str`, `loom::log`

## Full Roadmap

See `docs/PLAN.md` for the complete phased implementation plan.

## Checkpoint Workflow (MANDATORY)

Every phase follows a strict checkpoint loop. Nothing proceeds without user approval.

### Within a Phase

1. **Implement** a testable chunk of code (e.g., one or two related components).
2. **STOP.** Present a test plan to the user:
   - List every test case with expected behavior.
   - Explain what is being validated and why.
   - Wait for user approval before writing any test code.
3. **Write tests and run them.** Fix any failures.
4. **STOP.** Present a review to the user:
   - What was implemented.
   - How it was tested (which test cases, what they cover).
   - Test results (pass/fail counts, any issues found and how they were fixed).
   - Exact commands the user can run to verify themselves.
5. **Wait for user to run tests** and provide feedback or suggestions.
6. **Only then** continue to the next chunk within the phase or to the next phase.

### Between Phases

Before starting a new phase:
- All tests from the current phase must pass.
- User must have run the tests themselves and confirmed.
- Use `/handoff` to update tracking docs.
- Get explicit user approval to begin the next phase.

### Key Principle

**Nothing ships without the user seeing it, approving the test plan, running the tests, and giving the green light.** This catches design issues early and builds a reliable regression suite.

## Session Management

- **Session log**: `docs/SESSION_LOG.md` — rolling journal, newest first
- **Handoff**: Use `/handoff` command at end of session to update all tracking files
- **Plan tracking**: `docs/PLAN.md` uses checkboxes and markers (`← CURRENT`, `← NEXT UP`)

## Current State

- **Last session date**: 2026-02-04
- **Current phase**: Phase 0 — Project Scaffolding
- **Active branch**: master
- **Blockers**: None

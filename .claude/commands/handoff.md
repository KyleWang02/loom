End-of-session handoff. Perform ALL steps in order:

1. **Archive**: Read current `docs/SESSION_LOG.md`. Prepend its contents into `docs/ARCHIVE.md` (below the header/horizontal rule, above any existing entries). This preserves history in cold storage.

2. **Session log**: Overwrite `docs/SESSION_LOG.md` entirely with a single entry for THIS session:
   - Date
   - Focus (1 line)
   - Completed (bullet list)
   - Key Decisions (bullet list, only if meaningful choices were made)
   - Checkpoint Status (where in the implement→test→review loop we are)
   - Next (numbered priority list, 3-5 items)
   - Files Changed (list of created/modified files)
   - Include any noteworthy information/failures etc

3. **Plan**: Update `docs/PLAN.md` — check off completed items `[x]`, move `← CURRENT` / `← NEXT UP` markers.

4. **CLAUDE.md**: Update the "Current State" section at the bottom — date, phase, branch, blockers, test count.

5. **Architecture** (only if structure changed): Update `docs/ARCHITECTURE.md` with new modules, dependencies, or patterns.

6. **Report**: Print a brief summary of what was updated.

End-of-session handoff. Perform these steps:

1. **Update `docs/SESSION_LOG.md`**: Add a new entry at the TOP of the file (below the heading) with today's date and the following sections:
   - **Date**: Today's date
   - **Focus**: What was the goal for this session
   - **Completed**: Bullet list of what got done
   - **Key Decisions**: Design choices made during this session
   - **Issues & Gotchas**: Problems encountered
   - **Failed Approaches**: What didn't work and WHY (this is the most valuable section for future sessions)
   - **Checkpoint Status**: Where in the checkpoint workflow we are (e.g., "awaiting user test run for Phase 1 error handling", "user approved test plan, tests passing", etc.)
   - **Next Session Should**: Numbered priority list of what to do next
   - **Files Changed**: List of files created or modified (targeted reading list for next session)

2. **Update `CLAUDE.md` "Current State" section**: Set:
   - Last session date to today
   - Current phase to whatever phase we're on
   - Active branch to current git branch
   - Blockers to any known blockers (or "None")

3. **Update `docs/PLAN.md`**: Check off any completed items with `[x]`. Move the `← CURRENT` and `← NEXT UP` markers as needed based on progress.

4. **Review**: Note anything that should potentially be added to `CLAUDE.md` rules or architecture sections based on what was learned this session.

5. **Report**: Print a brief summary of what was updated in each file.

# Cyberpunk VR Port

## Keep what you learn

Agents start without memory of earlier sessions; these files are the memory. Without being asked, as part of the work:

- **Before you start**, look for notes on the area you touch: a `CLAUDE.md` or `*_NOTES.md` beside the code, and `docs/` and `notes/`.
- **Before you finish** a task in which you learned something a later agent could not get from the code or `git log`, write it down in the same change: the cause of a bug and how it was found, measurements, proved offsets, ideas that failed and why, misleading logs or tools, the user's decisions.
- **Where:** the existing notes file of that area, in the section it belongs to. If the area has none, add one in `docs/` (`<topic>-YYYYMMDD.md`) or a `<TOPIC>_NOTES.md` beside the code, and point to it from this file.
- **How:** facts with absolute dates and commit hashes. Say what is measured and what is only inferred, and correct an entry that turned out wrong rather than adding a second one. Do not repeat what the code already says.
- Notes follow the same rules as code: no commit or push without the user's go-ahead.

## Hand and gun collision

Before you change, test or measure how the hands or the gun meet the world -- in
`mods/cet/CyberpunkVRPort_HandCollision/` or in the native side (`src/Natives/DebugView.cpp`: movers, near list,
moving obstacles, probes; registered in `src/Natives/Registration.cpp`) -- read
`mods/cet/CyberpunkVRPort_HandCollision/COLLISION_NOTES.md`. It records what was measured in game, the bugs found,
the ideas already tried and rejected, the proved PhysX offsets and the working workflows. Do not redo what it settles,
and add what you learn to it in the same change.

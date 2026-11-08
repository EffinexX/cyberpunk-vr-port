# Cyberpunk VR Port

## Hand and gun collision

Before you change, test or measure how the hands or the gun meet the world -- in
`mods/cet/CyberpunkVRPort_HandCollision/` or in the native side (`src/Natives/DebugView.cpp`: movers, near list,
moving obstacles, probes; registered in `src/Natives/Registration.cpp`) -- read
`mods/cet/CyberpunkVRPort_HandCollision/COLLISION_NOTES.md`. It records what was measured in game, the bugs found,
the ideas already tried and rejected, the proved PhysX offsets and the working workflows. Do not redo what it settles,
and add what you learn to it in the same change.

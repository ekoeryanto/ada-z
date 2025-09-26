Cleanup performed

What I removed:

- src/storage_demo.cpp (inert pointer/demo removed)
- src/storage_integration_example.cpp (inert pointer/demo removed)
- include/Test_AI_0-10V.ino.sample (moved to docs earlier; removed from include/)
- docs/Test_AI_0-10V.ino.sample.copy (duplicate file removed)
- include/preferences_helper.h and src/preferences_helper.cpp (removed earlier)

Why:
- Demo/sample files shouldn't be compiled into firmware; they belong in `docs/`.
- Removing inert pointer files reduces noise.
- Preferences helper was consolidated into `include/storage_helpers.h`.

Verification:
- PlatformIO build ran successfully after these changes.

If you want me to also:
- Remove other small commented examples inside source files (e.g., comment blocks in `src/web_api.cpp`) — I can do that conservatively.
- Add a small `CONTRIBUTING.md` note about where to put examples vs. firmware code.

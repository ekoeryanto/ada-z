preferences_helper removed

The project previously provided a compatibility header and implementation named `preferences_helper` that wrapped Arduino `Preferences` calls. That code has been consolidated into `include/storage_helpers.h` which provides safer, per-operation NVS helpers and additional storage abstractions.

If you previously used:

- `safePreferencesBegin(void*, const char*)`
- `safeGetFloat(void*, const char*, float)`

Please call the appropriate functions in `storage_helpers.h` instead, for example:

- `loadFloatFromNVSns("config", key, defaultValue)`

Reason for removal:
- Simplifies the codebase by using a single, well-tested storage helper.
- Avoids duplicate symbols and confusing compatibility layers.

If you need assistance migrating code or restoring a compatibility shim, open an issue or ask in the repo.

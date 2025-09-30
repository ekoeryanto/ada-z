// nvs_defaults.h
#pragma once

// Ensure sensible defaults are written into NVS (Preferences) on first boot.
// Calling this once after nvs_flash_init() prevents repeated Preferences NOT_FOUND logs
// and guarantees keys exist for runtime reads.
void ensureNvsDefaults();

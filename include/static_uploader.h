#pragma once
#include <Arduino.h>
#include <FS.h>

// Extract a UStar/tar archive from an open File into destDir on the SD card.
// Returns true on success. This is a minimal extractor: supports regular files and directories,
// uses octal sizes from the tar header, and ignores other entry types.
bool extractTarToDir(File &tarFile, const String &destDir);

// Recursively remove a directory and its contents. Returns true on success.
bool removeDirRecursive(const String &path);

// Ensure parent directories for a path exist (creates them on SD). Returns true on success.
bool ensureParentDirs(const String &path);

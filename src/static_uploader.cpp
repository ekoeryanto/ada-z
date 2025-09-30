#include "static_uploader.h"
#include <SD.h>

static size_t octalToSize(const char *s, size_t len) {
    size_t v = 0;
    for (size_t i = 0; i < len && s[i]; ++i) {
        if (s[i] >= '0' && s[i] <= '7') {
            v = (v << 3) + (s[i] - '0');
        }
    }
    return v;
}

bool ensureParentDirs(const String &path) {
    // path is like /www/foo/bar.txt; create /www/foo
    int last = path.lastIndexOf('/');
    if (last <= 0) return true;
    String dir = path.substring(0, last);
    if (SD.exists(dir.c_str())) return true;
    // create recursively
    String cur = "";
    int start = 0;
    while (true) {
        int idx = dir.indexOf('/', start);
        if (idx == -1) idx = dir.length();
        cur += dir.substring(start, idx) + "/";
        if (!SD.exists(cur.c_str())) {
            SD.mkdir(cur.c_str());
        }
        if (idx == dir.length()) break;
        start = idx + 1;
    }
    return true;
}

bool removeDirRecursive(const String &path) {
    if (!SD.exists(path.c_str())) return true;
    File dir = SD.open(path.c_str());
    if (!dir) return false;
    if (!dir.isDirectory()) {
        dir.close();
        return SD.remove(path.c_str());
    }
    File entry = dir.openNextFile();
    while (entry) {
        String name = String(path) + "/" + entry.name();
        if (entry.isDirectory()) {
            entry.close();
            removeDirRecursive(name);
        } else {
            entry.close();
            SD.remove(name.c_str());
        }
        entry = dir.openNextFile();
    }
    dir.close();
    return SD.rmdir(path.c_str());
}

bool extractTarToDir(File &tarFile, const String &destDir) {
    // Minimal ustar reader: read 512-byte headers, then file data rounded up to 512 bytes
    const size_t BLOCK = 512;
    if (!tarFile) return false;
    // Ensure destDir exists
    if (!SD.exists(destDir.c_str())) {
        SD.mkdir(destDir.c_str());
    }

    while (tarFile.available()) {
        // read header
        uint8_t header[BLOCK];
        size_t r = tarFile.read(header, BLOCK);
        if (r != BLOCK) return false;
        bool allZero = true;
        for (size_t i = 0; i < BLOCK; ++i) if (header[i] != 0) { allZero = false; break; }
        if (allZero) break; // end of archive

        // parse name
        char namebuf[256];
        memset(namebuf, 0, sizeof(namebuf));
        memcpy(namebuf, header, 100);
        String name = String(namebuf);
        // parse size (octal at offset 124, length 12)
        size_t size = octalToSize((const char*)(header + 124), 12);
        // typeflag at 156
        char type = header[156];

        String outPath = destDir;
        if (!outPath.endsWith("/")) outPath += "/";
        outPath += name;

        if (type == '5') {
            // directory
            ensureParentDirs(outPath);
            if (!SD.exists(outPath.c_str())) SD.mkdir(outPath.c_str());
        } else {
            ensureParentDirs(outPath);
            // write file
            File out = SD.open(outPath.c_str(), FILE_WRITE);
            if (!out) return false;
            size_t remaining = size;
            // read file contents in chunks
            while (remaining > 0) {
                size_t toRead = remaining > BLOCK ? BLOCK : remaining;
                uint8_t buf[BLOCK];
                size_t got = tarFile.read(buf, toRead);
                if (got != toRead) { out.close(); return false; }
                out.write(buf, got);
                remaining -= got;
            }
            out.close();
            // skip padding to 512
            size_t pad = (BLOCK - (size % BLOCK)) % BLOCK;
            if (pad > 0) {
                tarFile.seek(tarFile.position() + pad);
            }
        }
    }
    return true;
}

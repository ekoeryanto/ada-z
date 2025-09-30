#!/usr/bin/env bash
# Deploy built web client to a mounted SD card's /www directory
# Usage:
#   ./scripts/deploy_webclient_to_sd.sh --dist client/dist --mount /Volumes/SDCARD
# If --mount is omitted the script will list /Volumes and prompt.

set -euo pipefail

DIST="client/dist"
MOUNT=""
DO_GZIP=1

usage() {
  cat <<EOF
Usage: $0 [--dist PATH] [--mount /Volumes/NAME] [--no-gzip]

Options:
  --dist PATH     Path to built client (default: client/dist)
  --mount PATH    Mount point of SD card (example: /Volumes/NO\ NAME or /Volumes/SDCARD)
  --no-gzip       Don't create .gz files alongside assets
  --help          Show this help

Example:
  $0 --dist client/dist --mount /Volumes/SDCARD

EOF
}

if [[ $# -gt 0 ]]; then
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --dist) DIST="$2"; shift 2;;
      --mount) MOUNT="$2"; shift 2;;
      --no-gzip) DO_GZIP=0; shift 1;;
      --help) usage; exit 0;;
      *) echo "Unknown option: $1"; usage; exit 2;;
    esac
  done
fi

if [[ ! -d "$DIST" ]]; then
  echo "Error: dist directory not found: $DIST" >&2
  exit 2
fi

if [[ -z "$MOUNT" ]]; then
  echo "Mounted volumes:" >&2
  ls /Volumes || true
  echo
  read -r -p "Enter SD mount path (e.g. /Volumes/SDCARD): " MOUNT
fi

if [[ ! -d "$MOUNT" ]]; then
  echo "Error: mount path not found: $MOUNT" >&2
  exit 2
fi

DEST="$MOUNT/www"
echo "Deploying '$DIST' -> '$DEST'"

# Create dest
mkdir -p "$DEST"

# Sync files (preserve directories, delete removed files in dest)
rsync -av --delete --exclude '.DS_Store' "$DIST/" "$DEST/"

if [[ $DO_GZIP -ne 0 ]]; then
  echo "Creating .gz siblings for common text assets (html, js, css, json, svg)"
  # Create gzipped counterparts in-place on the SD card (keeps original files)
  cd "$DEST"
  find . -type f \( -name '*.html' -o -name '*.js' -o -name '*.css' -o -name '*.json' -o -name '*.svg' \) -print0 \
    | xargs -0 -I{} sh -c 'gzip -9 -c "{}" > "{}".gz'
  echo "Gzip pass complete"
fi

echo "Deploy complete. Eject the SD card safely before removing." 
exit 0

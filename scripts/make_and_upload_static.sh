#!/usr/bin/env bash
# Package client/dist contents into a root tar (no leading folder) and upload to device
# Usage: ./scripts/make_and_upload_static.sh --host 192.168.1.50 --api-key MYKEY [--dist client/dist] [--port 80]
set -euo pipefail
HOST=""
API_KEY=""
DIST="client/dist"
PORT=80
URL=""

usage() {
  cat <<EOF
Usage: $0 --host HOST --api-key KEY [--dist PATH] [--port PORT] [--url URL]

This script creates a tar whose contents are the files inside DIST (no leading directory),
so the archive root contains index.html, assets/, etc. Then it uploads using the same
endpoint used by the firmware (/api/static/update) with header X-Api-Key.

Examples:
  $0 --host 192.168.1.50 --api-key Mar9aMulya
  $0 --url http://192.168.1.50/api/static/update --api-key Mar9aMulya
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --host) HOST="$2"; shift 2;;
    --api-key) API_KEY="$2"; shift 2;;
    --dist) DIST="$2"; shift 2;;
    --port) PORT="$2"; shift 2;;
    --url) URL="$2"; shift 2;;
    --help) usage; exit 0;;
    *) echo "Unknown option: $1"; usage; exit 2;;
  esac
done

if [[ -z "$URL" && -z "$HOST" ]]; then echo "--host or --url required"; usage; exit 2; fi
if [[ -z "$API_KEY" ]]; then echo "--api-key required"; usage; exit 2; fi
if [[ ! -d "$DIST" ]]; then echo "dist not found: $DIST"; exit 2; fi

if [[ -z "$URL" ]]; then URL="http://$HOST:$PORT/api/static/update"; fi

TMP_TAR=$(mktemp -t webclient-XXXXX.tar)

echo "Creating tar $TMP_TAR from directory contents: $DIST"
# Important: use -C to avoid leading directory entries and ensure archive root is file list
tar -C "$DIST" -cf "$TMP_TAR" .

if [[ ! -f "$TMP_TAR" ]]; then echo "Failed to create tar"; exit 2; fi

echo "Uploading to $URL"
curl -v --fail -H "X-Api-Key: $API_KEY" -F "file=@${TMP_TAR};filename=webclient.tar" "$URL"
RC=$?
rm -f "$TMP_TAR"
if [[ $RC -ne 0 ]]; then
  echo "Upload failed with code $RC"; exit $RC
fi

echo "Upload succeeded"
exit 0

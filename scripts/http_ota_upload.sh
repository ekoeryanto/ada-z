#!/usr/bin/env bash
# Simple shell uploader for press-32 OTA via HTTP endpoint (/api/update)
# Uses curl to POST multipart/form-data. Supports Bearer token or X-Api-Key header.
# Usage examples:
#   ./scripts/http_ota_upload.sh --host 192.168.111.241 --file .pio/build/usb/firmware.bin --token Mar9aMulya
#   ./scripts/http_ota_upload.sh --url http://192.168.111.241:80/api/update --file firmware.bin --api-key mykey

set -euo pipefail

usage() {
  cat <<EOF
Usage: $0 [--host HOST | --url URL] --file FILE (--token TOKEN | --api-key KEY) [--port PORT] [--https]

Options:
  --host HOST      Device host or IP (without scheme). Use with --port and optional --https.
  --url URL        Full URL to /api/update endpoint (overrides --host)
  --port PORT      HTTP port to use with --host (default: 80)
  --https          Use https:// when --host is provided
  --file FILE      Firmware file to upload (required)
  --token TOKEN    Bearer token for Authorization header ("Bearer <token>")
  --api-key KEY    X-Api-Key header value
  --help           Show this help

Examples:
  $0 --host 192.168.111.241 --file .pio/build/usb/firmware.bin --token Mar9aMulya
  $0 --url http://192.168.111.241:80/api/update --file firmware.bin --api-key mykey
EOF
}

HOST=""
URL=""
PORT=80
USE_HTTPS=0
FILE=""
TOKEN=""
API_KEY=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --host) HOST="$2"; shift 2;;
    --url) URL="$2"; shift 2;;
    --port) PORT="$2"; shift 2;;
    --https) USE_HTTPS=1; shift 1;;
    --file) FILE="$2"; shift 2;;
    --token) TOKEN="$2"; shift 2;;
    --api-key) API_KEY="$2"; shift 2;;
    --help) usage; exit 0;;
    --*) echo "Unknown option: $1"; usage; exit 2;;
    *) break;;
  esac
done

if [[ -z "$URL" && -z "$HOST" ]]; then
  echo "Error: --host or --url required" >&2
  usage; exit 2
fi
if [[ -z "$FILE" ]]; then
  echo "Error: --file required" >&2
  usage; exit 2
fi
if [[ -z "$TOKEN" && -z "$API_KEY" ]]; then
  echo "Error: provide --token or --api-key" >&2
  usage; exit 2
fi
if [[ ! -f "$FILE" ]]; then
  echo "Error: file not found: $FILE" >&2
  exit 2
fi

if [[ -z "$URL" ]]; then
  SCHEME="http"
  if [[ "$USE_HTTPS" -ne 0 ]]; then SCHEME="https"; fi
  URL="$SCHEME://$HOST:$PORT/api/update"
fi

# Build curl headers
CURL_HEADERS=( )
if [[ -n "$TOKEN" ]]; then
  CURL_HEADERS+=( -H "Authorization: Bearer $TOKEN" )
fi
if [[ -n "$API_KEY" ]]; then
  CURL_HEADERS+=( -H "X-Api-Key: $API_KEY" )
fi

# Use curl to POST multipart form. Field name 'update' matches Arduino Update upload handler expectations.
# --fail: exit non-zero on HTTP errors
# --progress-bar: show progress
# --max-time: overall timeout

set -x
curl --fail --progress-bar --max-time 120 \
  "${CURL_HEADERS[@]}" \
  -F "update=@${FILE};type=application/octet-stream" \
  "$URL"

EXIT_CODE=$?
if [[ $EXIT_CODE -eq 0 ]]; then
  echo "\nUpload completed successfully. Device may reboot."
else
  echo "\nUpload failed with exit code $EXIT_CODE" >&2
fi
exit $EXIT_CODE

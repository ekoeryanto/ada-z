#!/usr/bin/env bash
# Auto-calibrate all ADC sensors (AI1..AIN)
# Usage:
#   ./scripts/auto_calibrate_all.sh <device_ip> [target_or_map] [--dry-run]
# Examples:
#   # apply 4.8 bar span to all ADC sensors
#   ./scripts/auto_calibrate_all.sh 192.168.111.240 4.8
#
#   # apply per-sensor mapping (CSV): AI1:4.8,AI2:5.0
#   ./scripts/auto_calibrate_all.sh 192.168.111.240 "AI1:4.8,AI2:5.0"

set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "Usage: $0 <device_ip> [target_or_map] [--dry-run]"
  exit 2
fi

DEVICE_IP=$1
ARG2=${2:-}
DRY_RUN=0
if [ "${3:-}" = "--dry-run" ] || [ "${2:-}" = "--dry-run" ]; then
  DRY_RUN=1
fi

# Parse mapping CSV like AI1:4.8,AI2:5.0 into an associative array
declare -A MAP
DEFAULT_TARGET="4.8"

if [ -z "$ARG2" ]; then
  TARGET_ALL=$DEFAULT_TARGET
elif [[ "$ARG2" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
  TARGET_ALL=$ARG2
else
  TARGET_ALL=""
  IFS=',' read -ra PAIRS <<< "$ARG2"
  for p in "${PAIRS[@]}"; do
    k=${p%%:*}
    v=${p#*:}
    MAP["$k"]=$v
  done
fi

echo "Device: $DEVICE_IP"
if [ -n "$TARGET_ALL" ]; then
  echo "Target for all ADC sensors: $TARGET_ALL bar"
else
  echo "Using per-sensor mapping: ${!MAP[@]}"
fi
if [ "$DRY_RUN" -eq 1 ]; then echo "Dry-run enabled (no changes will be posted)"; fi

SENSORS_JSON=$(curl -s "http://${DEVICE_IP}/sensors/readings")
if [ -z "$SENSORS_JSON" ]; then
  echo "Failed to fetch sensors from device"
  exit 3
fi

# Iterate over ADC tags
count=0
echo "$SENSORS_JSON" | jq -c '.tags[] | select(.source=="adc")' | while read -r tag; do
  id=$(echo "$tag" | jq -r '.id')
  pin=$(echo "$tag" | jq -r '.port')
  filtered=$(echo "$tag" | jq -r '.value.filtered')
  raw=$(echo "$tag" | jq -r '.value.raw')
  curr_conv=$(echo "$tag" | jq -r '.value.converted.from_filtered')

  if [ -n "${MAP[$id]:-}" ]; then
    target=${MAP[$id]}
  else
    target=${TARGET_ALL}
  fi

  if [ -z "$target" ]; then
    echo "No target determined for $id; skipping"
    continue
  fi

  echo "Calibrating $id (pin $pin): raw=$raw filtered=$filtered current_conv=$curr_conv -> target=$target"

  if [ "$DRY_RUN" -eq 1 ]; then
    echo "DRY RUN: would POST span calibration for $id -> $target"
  else
    PAYLOAD=$(jq -n --argjson bn "{\"pin_number\": $pin, \"trigger_span_calibration\": true, \"span_pressure_value\": ($target|0+0)}" '$bn')
    # Using HTTP POST /calibrate/pin
    resp=$(curl -s -X POST "http://${DEVICE_IP}/calibrate/pin" -H 'Content-Type: application/json' -d "$PAYLOAD") || true
    echo "Response: $resp"
    # small pause to let device re-seed smoothed ADC if needed
    sleep 0.5
    # fetch updated reading for this pin
    updated=$(curl -s "http://${DEVICE_IP}/sensors/readings" | jq -c ".tags[] | select(.id==\"${id}\")") || true
    echo "Updated: $updated"
  fi
  count=$((count+1))
done

echo "Processed $count ADC sensors"

#!/usr/bin/env bash
# Apply ADS TP scale for a target pressure using device readings
# Usage: ./scripts/apply_ads_calibration.sh <device_ip> <ads_channel> <target_bar>

set -euo pipefail

if [ "$#" -lt 3 ]; then
  echo "Usage: $0 <device_ip> <ads_channel> <target_bar> [--dry-run]"
  exit 2
fi

DEVICE_IP=$1
ADS_CH=$2
TARGET_BAR=$3
DRY_RUN=0
if [ "${4:-}" = "--dry-run" ]; then
  DRY_RUN=1
fi

echo "Device: $DEVICE_IP  Channel: $ADS_CH  Target: $TARGET_BAR bar"

read_json() {
  curl -s "http://${DEVICE_IP}/sensors/readings" | jq -r "$1"
}

# Fetch current ma_smoothed for channel
MA_SM=$(read_json ".tags[] | select(.id==\"ADS_A${ADS_CH}\") | .meta.ma_smoothed")
if [ -z "$MA_SM" ] || [ "$MA_SM" = "null" ]; then
  echo "Failed to read ma_smoothed for ADS_A${ADS_CH}"
  exit 3
fi

echo "Current ma_smoothed: $MA_SM mA"

# Compute required scale (mv per mA)
# target_bar = (mv_per_ma * ma_smoothed) / 1000   => mv_per_ma = target_bar * 1000 / ma_smoothed
SCALE=$(python3 - <<PY
ma=${MA_SM}
target=${TARGET_BAR}
print(target*1000.0/ma)
PY
)

printf "Calculated tp_scale_mv_per_ma = %.6f\n" "$SCALE"

if [ "$DRY_RUN" -eq 1 ]; then
  echo "Dry run - not applying scale"
  exit 0
fi

PAYLOAD=$(jq -n --argjson ch "[{\"channel\": $ADS_CH, \"tp_scale_mv_per_ma\": $SCALE}]" '{channels:$ch}')

echo "Applying scale to device..."
curl -s -X POST "http://${DEVICE_IP}/ads/config" -H 'Content-Type: application/json' -d "$PAYLOAD" | jq '.'

echo "Waiting 2s for device update..."
sleep 2

echo "Fetching updated sensor reading..."
curl -s "http://${DEVICE_IP}/sensors/readings" | jq '.tags[] | select(.id=="ADS_A'${ADS_CH}'")'

echo "Done"

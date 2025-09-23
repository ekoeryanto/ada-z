# ADS Calibration & Automation

This document shows how to calibrate ADS channels (TP5551) remotely, with examples and an automation script.

Quick overview
- Use `GET /sensors/readings` to get the current `ma_smoothed` value for an ADS channel.
- Compute `tp_scale_mv_per_ma` as: `tp_scale_mv_per_ma = target_bar * 1000 / ma_smoothed`.
- POST the scale to `POST /ads/config` as JSON to persist it.

Examples

1) Read sensors (human readable):

```bash
curl -s "http://<device_ip>/sensors/readings" | jq '.'
```

2) POST a single-channel ADS scale:

```bash
curl -s -X POST "http://<device_ip>/ads/config" \
  -H 'Content-Type: application/json' \
  -d '{"channels":[ { "channel": 0, "tp_scale_mv_per_ma": 795.4416 } ] }' | jq '.'
```

3) Use the automation script `scripts/apply_ads_calibration.sh`:

```bash
# make executable once
chmod +x scripts/apply_ads_calibration.sh

# usage: scripts/apply_ads_calibration.sh <device_ip> <ads_channel> <target_bar>
./scripts/apply_ads_calibration.sh 192.168.111.240 0 2.1

# dry-run only computes and prints the scale
./scripts/apply_ads_calibration.sh 192.168.111.240 0 2.1 --dry-run
```

ADC Smoothing Example

1) Read the ADC smoothing config:

```bash
curl -s "http://<device_ip>/adc/config" | jq '.'
```

2) Update and persist `adc_num_samples` and `samples_per_sensor` (example):

```bash
curl -s -X POST "http://<device_ip>/adc/config" \
  -H 'Content-Type: application/json' \
  -d '{"adc_num_samples":16, "samples_per_sensor":4 }' | jq '.'
```

Notes
- The script computes the scale from the current smoothed mA reading and applies it; it waits 2s and fetches the updated reading for verification.
- The device will persist the scale in preferences; you can confirm by re-querying `GET /sensors/readings` and inspecting the sensor `meta.cal_tp_scale_mv_per_ma` field.

ADS Auto-Calibration Endpoint

You can ask the device to compute and persist the `tp_scale_mv_per_ma` (mV per mA) automatically using the current smoothed mA readings by calling `POST /ads/calibrate/auto`.

Body options:
- `{ "target": 2.1 }` -> apply target pressure to default ADS channels (0..1)
- `{ "channels": [ { "channel": 0, "target": 2.1 }, { "channel": 1, "target": 4.8 } ] }` -> per-channel targets

Example (apply 2.1 bar to channels 0..1):

```bash
curl -s -X POST "http://<device_ip>/ads/calibrate/auto" \
  -H 'Content-Type: application/json' \
  -d '{"target":2.1}' | jq '.'
```

The device will compute the required `tp_scale_mv_per_ma` using the currently smoothed current (mA) and persist it into the calibration namespace under keys like `tp_scale_0` and `tp_scale_1`. Re-query `GET /sensors/readings` to verify the applied `meta.cal_tp_scale_mv_per_ma` and the new `ma_smoothed` -> `converted` mapping.

Auto-calibrate All ADC Sensors

You can auto-apply a span calibration to every ADC sensor (AI1..AIx) with `scripts/auto_calibrate_all.sh`.

Examples:

```bash
# Apply 4.8 bar span to all ADC sensors
chmod +x scripts/auto_calibrate_all.sh
./scripts/auto_calibrate_all.sh 192.168.111.240 4.8

# Apply per-sensor mapping (CSV): AI1:4.8,AI2:5.0
./scripts/auto_calibrate_all.sh 192.168.111.240 "AI1:4.8,AI2:5.0"

# Dry-run to see actions without changing device
./scripts/auto_calibrate_all.sh 192.168.111.240 4.8 --dry-run
```

Behavior:
- Fetches `GET /sensors/readings`, iterates ADC sensors and for each posts `POST /calibrate/pin` with `trigger_span_calibration` and the requested `span_pressure_value`.
- After each apply, it fetches the updated reading and prints the result for verification.


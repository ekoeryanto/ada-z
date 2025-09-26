# press-32 — Firmware & OTA

This project targets an ESP32-based data-logger/RTU that exposes a JSON HTTP API for sensor readings, calibration, and remote update operations. It supports two OTA mechanisms:

- ArduinoOTA / espota (TCP-based OTA, typically port 3232)
- HTTP OTA via the `/update` endpoint (multipart/form-data upload)

This README documents the current OTA auth behavior and how to perform USB or remote updates.

## OTA authentication behavior

Starting with this codebase change, the firmware prefers a single shared credential stored in NVS (namespace `config`, key `api_key`):

- If `api_key` exists in NVS and is non-empty, the firmware will:
  - Use the NVS `api_key` as the ArduinoOTA (espota) password.
  - Use the same `api_key` value to authorize HTTP `/update` uploads (via `X-Api-Key` header or `Authorization: Bearer <key>`).

- If `api_key` is not present or empty, the firmware falls back to the compile-time `OTA_PASSWORD` macro for ArduinoOTA (unchanged behavior).

This makes it simple to rotate/update the secret at runtime by calling the `/config` endpoint (see below).

## Setting the API key (persist to NVS)

To set or change the API key used by both OTA mechanisms, send a POST to `/config` with JSON body:

```json
{ "api_key": "SOME_STRONG_SECRET" }
```

Example curl (replace `<device-ip>`):

```bash
curl -X POST http://<device-ip>/config -H "Content-Type: application/json" -d '{"api_key":"SOME_STRONG_SECRET"}'
```

After this, the device persists the key to NVS and will use it for subsequent OTA operations. If ArduinoOTA is already running, a reboot (or reinitialization) may be necessary for ArduinoOTA to pick up the new password.

## How to flash first time (USB)

If the running firmware does not yet include the `/update` handler or ArduinoOTA, you must flash the device at least once via USB.

1. Build & upload using PlatformIO (USB):

```bash
pio run -e usb -t upload -v
```

2. Open serial monitor to verify boot messages and OTA status (replace port):

```bash
pio device monitor --port /dev/tty.usbserial-XXXX --baud 115200
```

Look for messages like `OTA: started on port 3232` and HTTP server logs.

## Using PlatformIO espota (OTA)

If ArduinoOTA is active and the device is reachable on the network, use the `espota` env in `platformio.ini` or run:

```bash
pio run -e espota -t upload --upload-port <device-ip> -v
```

PlatformIO must pass `--auth` set to the secret used by the device. If you stored the API key in NVS, pass that value as `--auth`. Example:

```bash
pio run -e espota -t upload --upload-port 192.168.111.241 --upload-flags "--auth=YOUR_API_KEY" -v
```

> Note: `espota` uses a custom TCP protocol (not HTTP). Checking port 3232 with `nc -vz <device-ip> 3232` is a good way to confirm the device is listening.

## Using HTTP `/update` endpoint

If the firmware running on the device exposes `/update` (implemented in `src/web_api.cpp`), you can upload the firmware bin via HTTP multipart form upload. Example:

```bash
curl -v -F "file=@.pio/build/usb/firmware.bin" -H "X-Api-Key: YOUR_API_KEY" http://<device-ip>/update
```

The handler expects the `X-Api-Key` header or `Authorization: Bearer <key>` header. The device will write the received image using the `Update` API and reboot if successful.

## Recovery / fallback

If you cannot reach the device via network and it does not run OTA-capable firmware, you must perform a physical USB flash. If onsite access is not possible, consider shipping a small "recovery" firmware (WiFi+ArduinoOTA only) and have someone flash that first.

## Security notes

- Use a long random API key (16+ bytes) and rotate the key if suspected compromise.
- Avoid hardcoding secrets in source where possible; prefer runtime NVS storage.
- If possible, protect update operations behind a secure network or VPN. MCU-only HTTPS is difficult; use network-level protections where available.

---

If you want, I can:
- Add a small recovery environment and a minimal recovery binary in the repo.
- Update `platformio.ini` with a documented `espota` `--auth` placeholder or a helper script to run espota with the configured key.
- Add a sample `docs/ota.md` guide with copy-paste commands.

What would you like next?
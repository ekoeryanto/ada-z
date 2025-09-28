# Sistem Press-32 — Dokumentasi Lengkap

Dokumen ini menjelaskan arsitektur firmware **press-32**, alur kerja utama, modul-modul yang terlibat, hingga panduan operasi dan pemeliharaan. Gunakan dokumen ini sebagai referensi cepat ketika melakukan pengembangan, debugging, atau integrasi sistem baru.

---

## 1. Ringkasan Proyek

- **Target perangkat**: ESP32 (board `esp32dev`) yang berfungsi sebagai RTU/data logger untuk sensor tekanan 0–10 V dan 4–20 mA.
- **Fitur utama**:
  - Pembacaan analog (AI1–AI3) dengan kalibrasi per kanal.
  - Dukungan sensor arus berbasis ADS1115 + TP5551.
  - Penyimpanan data ke SD card, termasuk log error dan antrian notifikasi.
  - HTTP JSON API lengkap untuk konfigurasi, kalibrasi, monitoring, dan OTA update.
  - OTA via ArduinoOTA (TCP) dan HTTP `/update` dengan otentikasi API key.
  - Integrasi notifikasi HTTP webhook dan output serial.
  - Sinkronisasi waktu (RTC DS3231 + NTP) dengan fallback dan audit.

Referensi tambahan yang sudah ada di repo:
- `README.md` — ringkasan OTA & autentikasi.
- `docs/remote_calibration_quickstart.md` — langkah cepat kalibrasi jarak jauh.
- `docs/ads_calibration.md` — detail kalibrasi TP5551/ADS1115.
- `docs/openapi.yaml` — spesifikasi endpoint HTTP (perlu sinkronisasi jika ada perubahan API).

---

## 2. Perangkat Keras & Koneksi

| Komponen | Deskripsi |
| --- | --- |
| ESP32 | MCU utama, menjalankan firmware press-32. |
| AI1–AI3 | Input analog 0–10 V, dikonversi dengan pembagian tegangan dan kalibrasi linier. |
| ADS1115 @0x48 | ADC eksternal 16-bit untuk sensor arus. Channel 0–1 digunakan. |
| TP5551 | Penguat/konverter di rantai 4–20 mA (memiliki parameter `tp_scale`). |
| SD Card | Media penyimpanan log (`/datalog.csv`, pending notifications, `/error.log`). |
| RTC DS3231 | Modul waktu nyata; dapat diaktif/nonaktifkan via API. |

Pin penting dapat dilihat di `include/pins_config.h`. Pastikan referensi pin sesuai hardware produksi sebelum kompilasi.

---

## 3. Struktur Proyek

```
press-32/
├─ src/
│  ├─ main.cpp                  ← fungsi `setup()` & `loop()` utama
│  ├─ web_api.cpp               ← semua handler WebServer & OTA HTTP
│  ├─ voltage_pressure_sensor.* ← manajemen sensor 0–10 V
│  ├─ current_pressure_sensor.* ← manajemen ADS1115 4–20 mA
│  ├─ http_notifier.*           ← builder JSON untuk notifikasi
│  ├─ sd_logger.*               ← inisialisasi & utilitas SD card
│  ├─ sample_store.*            ← buffer ring in-memory + persistensi NVS
│  ├─ time_sync.*               ← sinkronisasi RTC + NTP
│  ├─ wifi_manager_module.*     ← WiFiManager dan event handler OTA/NTP
│  ├─ ota_updater.*             ← konfigurasi ArduinoOTA
│  └─ lainnya (config, helper, dll.)
├─ include/                     ← header publik
├─ docs/                        ← dokumentasi & contoh integrasi
├─ scripts/                     ← utilitas shell kalibrasi
├─ platformio.ini               ← konfigurasi PlatformIO (env `usb`, `espota`, dsb.)
└─ test/                        ← ruang untuk unit/integration test PlatformIO
```

---

## 4. Alur Boot & Loop Utama

1. **`setup()` (src/main.cpp)**
   - Inisialisasi Serial, I2C, dan NVS (`nvs_flash_init`).
   - Inisialisasi sensor arus (ADS1115) dan sensor tegangan (ADC internal + kalibrasi).
   - Membaca konfigurasi runtime (jumlah sample, enable/interval per sensor) dari NVS.
   - Menyiapkan SD logging (`setupSdLogger`).
   - Inisialisasi store sampel, modul waktu, Wi-Fi manager, OTA updater.
   - Menjalankan MDNS dan memulai server HTTP (`setupWebServer`).

2. **`loop()`**
   - `loopTimeSync()` untuk mengecek kebutuhan sync NTP/RTC.
   - Setiap `SENSOR_READ_INTERVAL` (default 5 detik):
     - Baca dan smoothing semua sensor tegangan (`updateVoltagePressureSensor`).
     - Update sample store dan hitung rata-rata.
     - Bila waktunya, kirim notifikasi batch (HTTP/serial).
     - Logging ke SD (`/datalog.csv` + data ADS).
   - Setiap 5 menit flush notifikasi pending dari SD jika Wi-Fi tersedia.
   - Cetak waktu RTC secara berkala.
   - Jalankan `handleOtaUpdate()` + `handleWebServerClients()` setiap iterasi.

---

## 5. Deskripsi Modul Utama

| Modul | Tanggung Jawab Kunci |
| --- | --- |
| `voltage_pressure_sensor.*` | - Karakterisasi ADC (`esp_adc_cal`)<br>- Memuat/simpan kalibrasi zero/span (per pin)<br>- Mengelola smoothing & saturasi<br>- Runtime `adcNumSamples` (bisa diubah via API) |
| `current_pressure_sensor.*` | - Setup ADS1115 & smoothing median/EMA<br>- Konversi mA → tekanan/depth<br>- Pengambilan parameter channel (shunt, gain, mode, `tp_scale`) dari NVS |
| `sample_store.*` | - Buffer ring per sensor (raw/smoothed/volt)<br>- Persistensi opsional ke NVS ketika wrap<br>- Hitung rata-rata untuk API dan notifikasi |
| `sd_logger.*` | - Mount SD, membuat header CSV<br>- Append log sensor, pending notifikasi, error log<br>- Mengatur flag `sd_enabled` di NVS |
| `http_notifier.*` | - Menyusun payload JSON (single/batch/ADS)<br>- Pastikan waktu valid (sinkron NTP jika perlu)<br>- Mengirim ke webhook atau serial sesuai mode |
| `time_sync.*` | - Abstraksi RTC DS3231 & sinkronisasi NTP<br>- Memberikan timestamp ISO, status RTC lost power, dsb. |
| `wifi_manager_module.*` | - Integrasi WiFiManager (autoConnect + portal AP)<br>- Event handler `STA_GOT_IP` → trigger NTP & OTA |
| `ota_updater.*` | - Setup ArduinoOTA (port, hostname, password dari `api_key`) |
| `web_api.*` | - Implementasi seluruh endpoint WebServer + OTA HTTP update |

---

## 6. Konfigurasi & Persistensi

Konfigurasi tersimpan dalam NVS (Preferences) dengan namespace berikut:

| Namespace | Key | Deskripsi |
| --- | --- | --- |
| `config` | `api_key` | API key untuk OTA & HTTP `/update` (juga password ArduinoOTA). |
| `config` | `notification_mode`, `notification_payload` | Mode output notifikasi & tipe payload. |
| `sensors` | `sensor_en_<idx>`, `sensor_iv_<idx>` | Enable flag & interval notifikasi per sensor. |
| `adc_cfg` | `num_samples`, `samples_per_sensor` | Jumlah sample ADC per pembacaan & kapasitas sample store. |
| `ads_cfg` | `ema_alpha`, `num_avg`, `shunt_<ch>`, `amp_<ch>`, `mode_<ch>` | Parameter smoothing & hardware ADS. |
| `cal` | `<pin>_zero_raw_adc`, `<pin>_span_pressure_value`, … | Kalibrasi tegangan (lihat `calibration_keys.h`). |
| `cal` | `tp_scale_<ch>` | Faktor konversi TP5551 mV/mA. |
| `sd` | `sd_enabled` | Flag enable SD card. |
| `time` | `rtc_enabled`, `last_ntp`, `last_ntp_iso` | Kontrol RTC & histori NTP. |
| `sstore` | `sbuf_<idx>`, `swi_<idx>`, `scnt_<idx>` | Dump sample store per sensor (untuk retensi di reboot). |

Ketika menambah parameter baru, pastikan menambah dokumentasi dan pembacaan/persistensi yang sesuai.

---

## 7. Alur Pembacaan Sensor

1. **Sensor Tegangan (AI1–AI3):**
   - Set resolusi 12-bit dan attenuasi 11 dB (`analogSetPinAttenuation`).
   - `updateVoltagePressureSensor(i)` mengambil beberapa sample (default 3) dengan delay 2 ms, lalu rata-rata.
   - Nilai disimpan di `smoothedADC[]` dan dikonversi ke tekanan menggunakan kalibrasi linier.
   - Deteksi saturasi: jika reading 4095 berturut ≥3 kali → `isPinSaturated()` menjadi `true`.

2. **Sensor Arus (ADS1115):**
   - Set gain `GAIN_TWOTHIRDS` (±6.144 V).
   - `readAdsMa()` membaca single-ended, menyimpan ke buffer channel, menghitung median dari `adsNumAvg`, lalu dihaluskan EMA (`adsEmaAlpha`).
   - Konversi ke tekanan memakai asumsi 0–10 bar (dapat disesuaikan via `tp_scale`).

3. **Sample Store:**
   - Setiap pembacaan menambahkan entri (raw, smoothed, volt) ke buffer ring.
   - Ketika buffer wrap, data dipersist ke NVS (perlu diperhatikan untuk umur flash; bisa dioptimasi jika diperlukan).

---

## 8. Logging & Notifikasi

- **CSV Logging**: `/datalog.csv` berisi timestamp + raw/smoothed/volt + data ADS (mV, mA, depth). Header dibuat otomatis bila file belum ada.
- **Pending Notifications**: payload JSON disimpan di `/pending_notifications.jsonl` jika pengiriman HTTP gagal; akan dicoba kembali setiap 5 menit.
- **Error Log**: `/error.log` merekam pesan error dengan timestamp ISO (via `logErrorToSd`).
- **Notifikasi HTTP/Serial**: ditangani oleh `http_notifier.cpp`. Payload detail memuat:
  - `timestamp`, `time_synced`, `rtu` (chip ID), dan array `tags`.
  - Tiap `tag` memuat `raw`, `filtered`, `scaled (volt)`, `converted (bar)`, metadata kalibrasi, saturasi, dll.

Konfigurasi notifikasi dapat diubah via `/notifications/config` (`mode` dan `payload_type`).

---

## 9. Web API Ringkas

> Semua endpoint berada di root WebServer (port 80 atau 8080 ketika WiFiManager portal). Header CORS telah mengizinkan `GET, POST, PUT, OPTIONS` dan header umum. Gunakan `Authorization: Bearer <api_key>` atau `X-Api-Key` sesuai kebutuhan.

| Endpoint | Metode | Deskripsi |
| --- | --- | --- |
| `/config` | GET/POST | Membaca/menyimpan konfigurasi dasar (termasuk `api_key`). |
| `/time/sync` | POST | Paksa sinkronisasi NTP. |
| `/time/status` | GET | Status sistem & RTC (epoch, ISO, last NTP). |
| `/api/system` | GET | Informasi sistem ringkas (IP, Wi-Fi, status RTC, waktu terakhir NTP). |
| `/api/config` | GET/POST | Membaca/menyimpan konfigurasi dasar (termasuk `api_key`). |
| `/api/time/sync` | POST | Paksa sinkronisasi NTP. |
| `/api/time/status` | GET | Status sistem & RTC (epoch, ISO, last NTP). |
| `/api/time/rtc` | GET/POST | Baca atau set RTC (ISO atau copy dari waktu sistem). |
| `/api/time/config` | GET/POST | Enable/disable RTC usage. |
| `/api/sensors/readings` | GET | Snapshot semua sensor AI + ADS lengkap dengan metadata. |
| `/api/tag` / `/api/tag/<TAG>` | GET | Pembacaan rata-rata sensor tertentu (mis. `AI1`). |
| `/api/calibrate` | GET/POST | Dapatkan atau set kalibrasi per sensor (zero/span/trigger). Mendukung field `target` + `samples`. |
| `/api/calibrate/auto` | POST | Set span otomatis untuk sensor AI berdasarkan nilai saat ini (pin/tag) dengan dukungan opsi `samples`. |
| `/api/calibrate/default` | POST | Terapkan kalibrasi default 0–10 bar ke semua sensor. |
| `/api/calibrate/default/pin` | POST | Terapkan default ke sensor tertentu (pin/tag). |
| `/api/adc/calibrate/...` | ... | Alias untuk endpoint kalibrasi ADC agar seragam. |
| `/api/ads/calibrate/auto` | POST | Hitung `tp_scale` berdasarkan pembacaan mA & target pressure. |
| `/api/ads/config` | GET/POST/PUT | Baca/set parameter channel ADS (shunt, gain, mode, smoothing). |
| `/api/adc/config` | GET/POST | Baca/set `adc_num_samples` dan `samples_per_sensor`. |
| `/api/sd/config` | GET/POST | Enable/disable penggunaan SD. |
| `/api/sd/error_log` | GET | Mengambil isi error log (opsional `?lines=`). |
| `/api/sd/error_log/clear` | POST | Mengosongkan error log. |
| `/api/sd/pending_notifications` | GET | Metadata (dan opsional konten) antrian notifikasi yang tersimpan di SD (`?include=1&lines=50`). |
| `/api/sd/pending_notifications/clear` | POST | Menghapus file `pending_notifications.jsonl` setelah backup manual. |
| `/api/notifications/config` | GET/POST | Atur mode dan payload notifikasi. |
| `/api/notifications/trigger` | POST | Trigger notifikasi (sensor tertentu, ADS channel, atau semua sensor). |
| `/api/update` | POST multipart | OTA via HTTP. Autentikasi wajib; merespon 401/500/200 sesuai status. |
| `/api/diagnostics/network` | GET | Status Wi-Fi (RSSI, SSID, alasan disconnect terakhir, jadwal reconnect/backoff). |

Detail payload dan contoh request tersedia di `docs/openapi.yaml` serta file dokumentasi lain di folder `docs/`.

---

## 10. Kalibrasi Sensor

### 10.1 Tegangan (0–10 V)

1. Tentukan sensor target via `pin`, `pin_index`, atau `tag` (`AI1`, `AI2`, ...).
2. Gunakan salah satu metode:
   - **Manual**: POST ke `/calibrate` dengan empat nilai eksplisit (`zero_raw_adc`, `span_raw_adc`, `zero_pressure_value`, `span_pressure_value`).
   - **Trigger Zero**: POST `{ "pin_index": 0, "trigger_zero_calibration": true }` ketika sensor berada di titik nol.
   - **Trigger Span**: POST `{ "pin_index": 0, "trigger_span_calibration": true, "span_pressure_value": 10.0 }` saat berada di tekanan target.
   - **Auto**: POST ke `/calibrate/auto` dengan `{ "target": 6.5 }` atau array spesifik: `{ "sensors": [{ "tag": "AI2", "target": 4.0 }] }`. Endpoint mengonsumsi buffer sample store; tambahkan `"samples": 10` bila ingin membatasi jumlah sampel rata-rata.
   - **Per-pin (langsung)**: POST ke `/calibrate/pin` dengan `{ "pin": 35, "target": 6.5, "samples": 12 }` untuk menetapkan span memakai 12 sampel terakhir. Respons menyertakan `measured_*`, jumlah sampel, dan apakah data berasal dari cache.
3. Endpoint `/calibrate/all` menyediakan snapshot seluruh kalibrasi untuk audit.

### 10.2 ADS / 4–20 mA

1. Pastikan channel memiliki arus stabil dan `getAdsSmoothedMa()` > 0.
2. POST ke `/ads/calibrate/auto` dengan target tekanan (bar):
   ```json
   {
     "channels": [
       { "channel": 0, "target": 5.0 },
       { "channel": 1, "target": 7.5 }
     ]
   }
   ```
   atau gunakan `{ "target": 6.0 }` untuk semua channel default (0–1).
3. Nilai `tp_scale_<ch>` di NVS akan diperbarui dan digunakan untuk konversi berikutnya.

> Semua endpoint kalibrasi sekarang menampilkan field diagnostik (`measured_raw_avg`, `samples_used`, dll.) sehingga hasil kalibrasi dapat diverifikasi segera. Gunakan query `samples` untuk menyesuaikan jendela rata-rata; bila nilai ini tidak tersedia, firmware otomatis memakai cache internal atau fallback ke pembacaan instan.

---

## 11. OTA & Keamanan

- **API Key**: Simpan melalui `/config` (`{ "api_key": "SECRET" }`). Nilai ini otomatis dipakai sebagai:
  - Password ArduinoOTA (`espota`).
  - Token validasi untuk endpoint `/update` (via header `X-Api-Key` atau `Authorization: Bearer`).
- **ArduinoOTA**:
  ```bash
  pio run -e espota -t upload --upload-port <IP> --upload-flags "--auth=<API_KEY>"
  ```
- **HTTP OTA**:
  ```bash
  curl -F "file=@.pio/build/usb/firmware.bin" \
       -H "X-Api-Key: <API_KEY>" \
       http://<device-ip>/update
  ```
- Respons `/update`:
  - `401` jika API key salah.
  - `500` bila terjadi error `Update`.
  - `200` bila sukses (perangkat akan reboot otomatis).
- Pastikan jaringan internal tetap aman meski server tidak dipublikasikan. Jika endpoint akan dibuka ke publik, tambahkan rate limiting atau reverse proxy yang aman.

---

## 12. Build, Upload, & Monitoring

### 12.1 Prasyarat
- PlatformIO (CLI), Python 3.11.
- Toolchain ESP32 (diinstal otomatis oleh PlatformIO).
- Hak akses ke direktori `~/.platformio` (jika muncul error permission, perbaiki sebelum build).

### 12.2 Perintah Utama
```bash
# Build untuk flash via USB
pio run -e usb

# Upload via USB (ganti port serial sesuai OS)
pio run -e usb -t upload --upload-port /dev/ttyUSB0

# Monitor serial (115200 baud)
pio device monitor --port /dev/ttyUSB0 --baud 115200

# Build + upload OTA (lihat bagian OTA untuk flag auth)
pio run -e espota -t upload --upload-port <IP> --upload-flags "--auth=<API_KEY>"

# Jalankan environment dev/test lainnya jika ditambahkan di platformio.ini
```

### 12.3 Opsi Build Tambahan
- `platformio.ini` mem-build dengan `-Os` dan garbage collection section untuk mengecilkan ukuran firmware.
- Verbose logging dapat diaktifkan dengan menambahkan `-DENABLE_VERBOSE_LOGS=1` pada `build_flags` (default dimatikan).

> Catatan: pastikan direktori home `.platformio` dapat ditulis. Jika build gagal karena lock file, periksa kepemilikan folder atau jalankan perintah dengan user yang sama yang menginstal PlatformIO.

---

## 13. Debugging & Troubleshooting

- **Wi-Fi gagal terkoneksi**: Portal WiFiManager akan aktif. Cek log serial untuk SSID & password default (`DEFAULT_SSID`, `DEFAULT_PASS`).
- **Wi-Fi sering drop**: Firmware sekarang melakukan reconnect otomatis dengan exponential backoff (5 s → 5 menit) dan mengekspos statusnya via `/diagnostics/network`.
- **RTC tidak ditemukan**: Pastikan modul DS3231 terhubung ke SDA/SCL. Status dapat dicek via `/time/status`. Jika `rtc_lost_power = 1`, jalankan `/time/rtc` POST dengan `"from_system": true` setelah NTP valid.
- **SD card error**: Endpoint `/sd/config` dapat men-disable sementara. Cek wiring `SD_CS`. Log error berada di `/error.log` (ambil via `/sd/error_log`).
- **Kalibrasi berantakan setelah reboot**: Pastikan nilai zero/span tersimpan (cek via `/calibrate/all`). Jika sample store memakan flash terlalu sering, pertimbangkan mengurangi `samples_per_sensor` atau memindah persistensi ke SD.
- **Webhook gagal**: Cek log serial untuk HTTP response code. Bila tidak ada internet, payload akan masuk ke `pending_notifications.jsonl` dan di-flush saat koneksi kembali.
- **OTA gagal dengan 500**: Periksa ukuran firmware (`ESP.getFreeSketchSpace()`) dan pastikan tidak melebihi partisi. Cek log serial untuk pesan `Update Error`.

---

## 14. Rekomendasi Pengembangan Selanjutnya

- **Testing**: Tambahkan unit test (folder `test/`) untuk fungsi utilitas (mis. konversi mA → bar, persistensi sample store) serta integration test HTTP menggunakan `unity` + `WiFiClient`.
- **Dokumentasi API**: Sinkronkan `docs/openapi.yaml` dengan status terbaru (terutama response error `/update`, body baru `/calibrate/auto`).
- **Optimasi NVS Write**: Pertimbangkan pengurangan frekuensi persistensi sample store atau memindahkan ke SD/LittleFS untuk meningkatkan usia flash.
- **Keamanan**: Jika sistem akan diekspos di jaringan yang lebih luas, tambahkan autentikasi pada endpoint sensitif (`/config`, `/calibrate`, `/notifications/trigger`).
- **Monitoring**: Tambahkan endpoint kesehatan (mis. `/health`) dengan informasi ringkas uptime, free heap, status koneksi.

---

## 15. Referensi Cepat

- **File konfigurasi utama**: `include/config.h` — SSID default, interval, URL webhook.
- **Skema API**: `docs/openapi.yaml`.
- **Script kalibrasi**: `scripts/auto_calibrate_all.sh`, `scripts/apply_ads_calibration.sh`.
- **Contoh integrasi penyimpanan**: `docs/storage_integration_example.cpp`, `docs/storage_demo.cpp`.
- **Dokumentasi tambahan**: `docs/remote_calibration_quickstart.md`, `docs/ads_calibration.md`, `docs/CLEANUP.md` (catatan migrasi).

---

### Kontak & Kontribusi
- Gunakan `git` branch terkait saat menambah fitur atau mengubah struktur modul.
- Sertakan pembaruan dokumentasi (termasuk file ini) setiap kali API atau flow utama berubah.
- Lakukan code review internal sebelum merge ke `main` agar konsistensi dan stabilitas firmware terjaga.

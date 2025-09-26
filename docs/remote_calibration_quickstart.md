# Remote Calibration Quickstart

Jika Anda tidak bisa upload firmware saat ini, Anda masih bisa memperbaiki kalibrasi lewat endpoint HTTP yang tersedia di perangkat.

Sebelum mulai:
- Pastikan perangkat terhubung ke jaringan dan Anda bisa mengakses HTTP API (port 80 atau 8080). Jika `curl http://<device_ip>/sensors/readings` berhasil, Anda siap.

1) Verifikasi bacaan saat ini

```bash
curl -s "http://<device_ip>/sensors/readings" | jq '.'
```

Perhatikan objek ADS (`id`: `ADS_A0`, `ADS_A1`) dan bidang `meta.ma_smoothed` serta `meta.cal_tp_scale_mv_per_ma`.

2) Auto-calibrate ADS (gunakan pembacaan mA saat ini)

- Terapkan satu target ke semua channel ADS (default 0..1):

```bash
curl -s -X POST "http://<device_ip>/ads/calibrate/auto" \
  -H 'Content-Type: application/json' \
  -d '{"target":2.1}' | jq '.'
```

- Terapkan target per-channel:

```bash
curl -s -X POST "http://<device_ip>/ads/calibrate/auto" \
  -H 'Content-Type: application/json' \
  -d '{"channels":[{"channel":0,"target":2.1},{"channel":1,"target":4.8}] }' | jq '.'
```

Catatan: Endpoint menghitung `tp_scale_mv_per_ma` secara otomatis dari `ma_smoothed` dan menyimpannya. Setelah itu, panggil `GET /sensors/readings` untuk verifikasi perubahan.

3) Set tp_scale secara manual via `/ads/config`

```bash
curl -s -X POST "http://<device_ip>/ads/config" \
  -H 'Content-Type: application/json' \
  -d '{"channels":[{"channel":0, "tp_scale_mv_per_ma": 795.44}] }' | jq '.'
```

4) Auto-calibrate ADC (gunakan nilai smoothed ADC sebagai span)

- Terapkan target ke semua sensor ADC:

```bash
curl -s -X POST "http://<device_ip>/adc/calibrate/auto" \
  -H 'Content-Type: application/json' \
  -d '{"target":4.8}' | jq '.'
```

- Terapkan target per-pin:

```bash
curl -s -X POST "http://<device_ip>/adc/calibrate/auto" \
  -H 'Content-Type: application/json' \
  -d '{"sensors":[{"pin":35, "target":4.8}] }' | jq '.'
```

5) Blocking span calibration for single ADC pin (ambil rata2 N sampel)

```bash
curl -s -X POST "http://<device_ip>/calibrate/pin" \
  -H 'Content-Type: application/json' \
  -d '{"pin":35, "value":4.8, "samples":20, "sample_delay_ms":50 }' | jq '.'
```

6) Verifikasi kalibrasi

```bash
curl -s "http://<device_ip>/calibrate?pin_index=0" | jq '.'
curl -s "http://<device_ip>/calibrate/all" | jq '.'
curl -s "http://<device_ip>/ads/config" | jq '.'
```

Troubleshooting singkat
- Jika ping gagal tapi HTTP berhasil: kemungkinan ICMP diblokir atau perangkat berada di jaringan ter-isolasi; gunakan HTTP endpoints saja.
- Setelah kalibrasi ADC/ADS, kode memanggil `setupVoltagePressureSensor()` untuk reseed smoothing sehingga nilai yang terlihat di API langsung berubah.
- Jika hasil tidak sesuai, ulangi kalibrasi setelah memastikan sensor terisi (span) atau kosong (zero) sesuai target.

Jika mau, saya bisa:
- Jalankan contoh `curl` yang Anda pilih (Anda cukup beri IP dan target), atau
- Buat skrip kecil untuk otomatisasi kalibrasi per-sensor berdasarkan input Anda.

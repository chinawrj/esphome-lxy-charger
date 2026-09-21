# Verification record

ESPHome 2026.9.0 / ESP-IDF 5.5.5.

## Automated checks

- Native captured-frame encoding and decoder tests: PASS.
- Native queued-event routing: PASS for every LCD/Button/LED/Web combination.
- Queue capacity, non-reentrant dispatch, correlation IDs, stale readiness, and UI feedback versus BLE busy state: PASS.
- ESPHome configuration validation: 16/16 PASS.
- Full ESP32 firmware compilation: 16/16 PASS. Compiled component copies were compared with final repository source.
- ATOMS3U / ESP32-S3 build: PASS; no ATOMS3U hardware was connected.

See [test-matrix.json](test-matrix.json) for the matrix results. The bit order is LCD, Button, LED, Web; BLE and the event bus are always present. Matrix tests use example credentials and do not upload firmware.

## Physical M5StickC Plus 1.1

The first-stage headless + Web profile was uploaded and tested against a compatible charger with its charging output disabled by the operator.

1. Boot and auto-connect discovered GATT, subscribed to notifications, and read back 58.4 V / 5.1 A.
2. Editing a draft was followed by a fresh configuration query, proving the charger configuration remained unchanged before Apply.
3. Four explicit Apply operations were independently verified: current 5.0 A, restore 5.1 A, voltage 58.3 V, restore 58.4 V.
4. Disconnect invalidated readiness and readback values. Reconnect read back the restored configuration without replaying Apply.

The complete LCD/Button/LED/Web profile was subsequently uploaded using authenticated ESPHome OTA. Boot, networking and BLE readback pass. Physical display/LED/button observations are pending final confirmation.

The sixteen combinations were not each uploaded to physical hardware. These checks do not establish actual electrical output, charging performance, power-cycle persistence, or compatibility with different charger protocols. Original radio captures and device/network identifiers are excluded from this public report.

# Verification record

ESPHome 2026.9.0 / ESP-IDF 5.5.5.

## Automated checks

- Native captured-frame encoding and decoder tests: PASS.
- Native queued-event routing: PASS for every LCD/Button/LED/Web combination.
- Queue capacity, non-reentrant dispatch, correlation IDs, stale readiness, and UI feedback versus BLE busy state: PASS.
- Real BLE component compiled against a fake clock and GATT transport: PASS for serialized polling, one deferred request, immutable payloads, cancellation, overflow correlation and no replay.
- ESPHome configuration validation: 16/16 PASS.
- Full ESP32 firmware compilation: 16/16 PASS. Compiled component copies were compared with final repository source.
- ATOMS3U / ESP32-S3 build: PASS; no ATOMS3U hardware was connected.

See [test-matrix.json](test-matrix.json) for the matrix results. The bit order is LCD, Button, LED, Web; BLE and the event bus are always present. Matrix tests use example credentials and do not upload firmware.

Independent Linux CI also passed **18/18 jobs** for code commit `d95569441e9371ad8649ee7e20e29b116d0fda17`: native tests and YAML validation, all sixteen firmware combinations, and ATOMS3U compilation. [GitHub Actions run 35574823068](https://github.com/chinawrj/esphome-lxy-charger/actions/runs/35574823068). Subsequent verification-document changes do not alter that tested code.

## Physical M5StickC Plus 1.1

The final headless + Web profile and the complete LCD/Button/LED/Web profile were each uploaded and tested against a compatible charger with its charging output disabled by the operator. Both completed the same 15-step Web exercise:

1. Boot and auto-connect discovered GATT, subscribed to notifications, and read back 58.4 V / 5.1 A.
2. Editing a draft was followed by a fresh configuration query, proving the charger configuration remained unchanged before Apply.
3. Four explicit Apply operations were independently verified: current 5.0 A, restore 5.1 A, voltage 58.3 V, restore 58.4 V.
4. Disconnect invalidated readiness and readback values. Reconnect read back the restored configuration without replaying Apply.

Both exercises finished with 58.4 V / 5.1 A restored. The complete profile remains installed using authenticated ESPHome OTA. On 2026-09-21 the operator confirmed that physical LCD display verification passed. Independent observations of the red LED and physical A button have not yet been reported; their configuration and event paths are included in the automated coverage above.

## Status-poll serialization regression

A full-profile test before the fix encountered an unconfirmed Apply; it was not retried, and a subsequent read returned the original configuration. Targeted read-only probes then reproduced a concrete timing defect twice: sending `02` while `04` was still waiting for `84` produced no `82`. A third probe sent after `84` succeeded.

After the fix, three targeted probes all queued the user request until `84`, then sent one `02` and received `82`, without timeout or reconnection. This was verified with serial TX/RX evidence, not merely the final Web value: reconnect recovery can otherwise make an unsuccessful refresh appear complete. The final normal-log-level builds then passed the complete headless and LCD-profile Web exercises described above.

The final LCD-profile serial log also records a user Apply waiting for a background status reply, then sending once, matching its echo, and passing the independent readback. Thus the hardware evidence covers both deferred reads and a deferred Apply.

The observed collision explains a reproducible failure path. The original Apply timeout had no frame-level diagnostic log, so its exact cause is not independently proven.

The sixteen combinations were not each uploaded to physical hardware. These checks do not establish actual electrical output, charging performance, power-cycle persistence, or compatibility with different charger protocols. Original radio captures and device/network identifiers are excluded from this public report.

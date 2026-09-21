# Verification record

## Power-focused meter and expanded setpoint ranges — 2026-09-21

Code commit `77fe8c4` includes the power-focused layout (`80e84fc`) and requested setting ranges: **50.0–93.0 V / 1.0–10.0 A**, in 0.1 steps. Voltage follows the operator-reported 50–93 V nameplate. Current follows the operator's explicit 1–10 A request; the reported nameplate says 3–10 A. Acceptance below 3 A has not been physically tested.

- Native real-BLE fake-transport tests passed for all **431 voltage steps and 91 current steps**, including single-send payload bytes, matching echo, independent readback, and rejected nonfinite/off-step/out-of-bounds requests. This is not a claim of testing every V/A pair or electrically operating the charger throughout these ranges.
- Web staging tests cover those same steps without automatic Apply. Real button/controller tests cover both voltage and current endpoints, stopping at bounds, a separate confirmation gesture, unchanged companion setpoints and rejection of invalid baselines. Existing transaction, wake and telemetry regressions passed.
- The meter gives power **76 px** type, with **28 px V/A on one footer row**; longer power strings shrink. All twelve shared-view-model render scenes passed text bounds and glyph checks. Meter previews are synthetic/layout evidence, not physical LCD photographs. The 15-second idle entry and consumed wake gesture are unchanged.
- All **16/16 module combinations** passed YAML validation, full ESP-IDF compilation and exact source-copy/compiled-component checks. [Current matrix](test-matrix.json): `voltage-current-range-20260921T111823Z-f8dab5dbffd2`. Separate M5StickC Plus, ATOMS3U and generic ESP32 builds passed; ATOMS3U remains compile-only.
- M5Stick OTA succeeded for the final 19:18:24 +0800 build (configuration hash `0x5648fe93`). Read-only device Web metadata confirms voltage min/max/step **50.0/93.0/0.1**, current **1.0/10.0/0.1**, and `Connected (ready)`. Configuration readback remained **58.2 V / 5.1 A**. No setting command was sent for these updates.
- Current telemetry remains deferred; power stays unavailable until both same-sample output channels are fresh and valid. Voltage remains provisional, marked `V*`. Expanded-range physical writes, new subjective layout acceptance and this revision's separate GitHub CI completion are not claimed here.

---

## Idle V/A/W meter follow-up — 2026-09-21

Code commit `b2b785b` adds a 15-second home-page inactivity transition to a dedicated V/A/W meter. The first press wakes home and consumes the entire gesture; no BLE request or editing action accompanies wake. Button drives `UI_STATE`; LCD reads the event snapshot. The earlier 10% LED duty adjustment is retained.

- Native real-controller/wrapper tests passed for the exact idle boundary, wake on short/long/combined presses, next separate button action, editing isolation, absent LCD, heartbeat loss, and timer/sequence rollover. Existing transaction and button safety regressions also passed.
- The real reducer/view tests passed for same-sample `V × A`, separation from setpoints, voltage-only data, real zero values, stale data and disconnection. Missing current always leaves power unavailable. Event-core, BLE and all 32 Web-entity tests also passed.
- All **16/16 module combinations** passed YAML validation, full ESP-IDF builds and exact source-copy/compiled-component checks. [Archived idle-meter matrix](https://github.com/chinawrj/esphome-lxy-charger/blob/8a984d5/docs/test-matrix.json): `idle-meter-20260921T105257Z-5f4b4679a5aa`. This report supersedes the historical matrices below.
- The full M5StickC Plus 1.1 build passed and was installed by successful OTA, configuration hash `0xe79fa07d`. Read-only post-OTA checks reported `Connected (ready)`. No charger-setting write was sent for this update.
- Eleven shared-view-model layout scenes passed text bounds and glyph checks, including voltage-only, synthetic V/A/W and stale meter pages. These are previews, not hardware photos. Physical idle/wake behavior and subjective legibility have been requested from the operator but are not yet independently confirmed.
- Voltage is still provisional, marked `V*` on the meter; current decoding remains deferred. The local test results above do not claim completion of this revision's separate GitHub Actions run.

---

## v2.1 voltage decoder and local controls — 2026-09-21

The operator deferred live-current decoding until a later battery test, declined further mini-program use, and requested implementation from the new restart data. This revision decodes **voltage only**, with an explicit inferred-mapping label. It does not certify electrical accuracy. Current remains unavailable; current setpoint control is unchanged.

- Native tests passed for the real BLE parser/transaction service, event core, display, Button/LED wrappers and Web adapter. Captured restart bytes exercise 0.0/0.1/25.9/58.7–59.0 V. Tests reject wrong lengths, damaged checksums and out-of-envelope values; cover channel masks, discarded unsupported current values, capability changes, stale/offline data, and all 32 optional Web-entity combinations.
- All **16/16** module combinations passed YAML validation, full ESP-IDF compilation, included-component checks and byte-for-byte source-copy checks. [Archived v2.1 matrix](https://github.com/chinawrj/esphome-lxy-charger/blob/78432b775592cac114ed1ad861a2d2cc317ecd79/docs/test-matrix.json): `voltage-restart-20260921T094715Z-7bac925a9f60`. Independent generic ESP32, ATOMS3U and full M5StickC Plus builds passed. ATOMS3U is compile-only.
- The final full profile was installed on the original M5StickC Plus 1.1 through successful OTA, configuration hash `0x388b6511`. No charger-setting write was sent for this decoder update.
- After the operator restarted the charger again, the device reconnected automatically. A 180-second read-only Web observation recorded **131 polls**. Among **106 ready/valid observations bracketed by identical raw frames, 106 matched** the voltage formula and kept current unavailable; no checked mismatch occurred. Observed decoded values included 0.0, 44.2, 58.7, 58.8, 58.9 and 59.0 V. Changing frames were excluded from comparison because the HTTP reads are sequential. These are software-path checks, not independent voltage measurements.
- The operator explicitly reported **58.9 V on the physical LCD** after the second restart. Earlier in this revision cycle the operator confirmed the Chinese display, button responses and steady red LED. This does not claim a new physical Button Apply exercise; explicit setting writes/readback were exercised in the historical baseline below.
- The [current Web screenshot](images/web-ui.png) shows the running decoder, **58.9 V**, current **NA**, and separate **58.4 V / 5.1 A** setpoints. A GET-only local proxy preserved the original device HTML and live SSE values; only the diagnostics area was cropped. The eight LCD images are shared-view-model renders, not hardware photographs or calibration evidence. All text bounds and bundled glyph checks passed.
- The voltage mapping remains provisional (`84` DATA[3:5], big-endian / 10), clearly indicated on LCD and Web. [Protocol evidence and limits](protocol.md) distinguish captured facts from inference. Current, temperature, output-switch decoding, wider setpoint ranges and absolute electrical accuracy remain outside this release's verified scope.

Independent Linux CI passed **18/18 jobs** for voltage-decoder commit `631829806a7e352373669b56177849ef8cf7397e`: native tests and YAML validation, all sixteen module firmware combinations, and ATOMS3U compilation. [GitHub Actions run 35585830385](https://github.com/chinawrj/esphome-lxy-charger/actions/runs/35585830385). The subsequent release-verification documentation commit does not change that tested code.

The earlier run [35583676928](https://github.com/chinawrj/esphome-lxy-charger/actions/runs/35583676928) passed 18/18 jobs for **5138d93**, the preceding UE checkpoint. It is retained only as historical evidence.

---

## Historical v2.1 UE checkpoint before voltage decoding — 2026-09-21

This checkpoint adds the local button editor, Chinese LCD status/help views, a connection-only LED indicator and separate live-output display/event interfaces. **It is not a completed live-telemetry release:** the BLE `84` output V/A mapping is still unverified and the decoder is not enabled. The operator reports an unloaded charger with no battery connected; that does not identify which zero-valued payload fields represent output measurements. No public v2.1 release has been made at this checkpoint.

- Native tests passed for the real event reducer and display view, physical-button controller and ESPHome wrapper, independent LED controller/wrapper, and real Web component. The Web tests cover all **32** combinations of its five optional live-output/status entities; this is separate from the 16 module combinations.
- Regression coverage includes disabled telemetry capability rejecting late samples, all seven output-data states, raw replies not renewing measurement freshness, timer wraparound, connection intent versus actual connection, HELP and release-to-confirm controls, and a pending request finishing during a held button gesture.
- Protocol decoder and BLE request-serialization regressions passed; UI events do not modify BLE readiness, busy state or transaction correlation.
- All **16/16** optional module combinations passed YAML validation and complete ESP-IDF compilation. Custom C++/header copies match the final tested sources byte-for-byte, and compiled units match the selected modules. See the [archived UE matrix](https://github.com/chinawrj/esphome-lxy-charger/blob/5138d9323c1fe8d32463fc1559fb2f26d110c1e2/docs/test-matrix.json), run `ue-connection-20260921T092250Z-bb412cea9f5a`. Its source manifest also covers the bundled UI fonts and glyph inventory.
- Separate generic ESP32 headless, ATOMS3U and full M5StickC Plus builds passed. ATOMS3U remains compile-only.
- The final UE full profile was installed successfully on the original M5StickC Plus 1.1 using OTA (build configuration hash `0x0628ffed`). Authenticated read-only Web checks after installation reported `Connected (ready)`, `BLE connected; output decoding not implemented`, ready=true and unchanged readback **58.4 V / 5.1 A**. Measured output values were unavailable, as expected while decoding is disabled. This verification did not change charger parameters.
- That checkpoint had a read-only Web snapshot and seven shared-view-model renders. The current images linked above supersede them after the voltage-decoder update; no old image is presented as current evidence.
- After this OTA installation the operator confirmed the revised Chinese LCD, button response and steady red LED. This is a user-reported physical observation, separate from the automated view/controller checks; it does not establish a new physical Apply-and-readback exercise or measured-output decoding. The fixed red LED now encodes only the BLE connection: steady when connected, slow blinking while connection is enabled but disconnected, off when disabled/disconnected. Blinking/off transitions have automated coverage but have not been independently reported by the operator. The LED does not indicate charging output or measurement validity.

This historical checkpoint used its own archived matrix. The current matrix report refers to the newer voltage-decoder source above; the physical setpoint exercise below refers specifically to v2.0.0.

---

## v2.0.0 baseline

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

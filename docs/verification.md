# Verification record

## Five-minute idle backlight and LED — 2026-09-21

Code commit `4d10bf3` includes the 300000 ms backlight timer from `c30e31e` and adds LED suspension during the same idle interval. The timer spans Home and Meter. Button publishes `UI_STATE.ui_backlight_on`; LCD owns the AXP192 LDO2 update and confirms its readback. Button absence/unavailability keeps the backlight on and the normal BLE LED pattern available. LED suspension requests zero duty with no idle pulses; this does not put the ESP32 into deep sleep. BLE, Wi-Fi, battery sampling and LCD heartbeats continue while dark.

- Native tests passed the exact 299999/300000 ms boundary, Home-to-Meter continuity, telemetry independence, short/long A/B wake, simultaneous wake, consumed gestures, separate next-command behavior, missing edges, clock rollover, absent/lost LCD, held keys, Help/edit timeouts and pending operations. Tests exercise the real Button wrapper, event snapshot and LCD request helper, including Button removal. All 256 rail masks in both directions preserve every bit except AXP192 `0x12` bit 2; failed reads/writes/readback verification can be retried.
- All **32/32 module configurations** passed YAML validation and full firmware compilation, with exact source-copy and compiled-component checks. [Current matrix](test-matrix.json): `idle-display-led-20260921T131425Z-c2e00889c073`.
- Independent Linux CI [run 35604385256](https://github.com/chinawrj/esphome-lxy-charger/actions/runs/35604385256) passed the native/YAML job for `4d10bf3`; remote firmware jobs were still running/queued when recorded.
- LED tests also cover zero-duty idle across all connected/enabled states and pulse phases, no repeated output writes while idle, Button removal, consumed wake and restoration of the current BLE pattern.
- Existing event-core (32 routing profiles), real BLE transaction, telemetry view, board battery and Web (32 entity profiles) tests passed. Separate M5StickC Plus, ATOMS3U and generic ESP32 firmware builds passed; only M5Stick was uploaded.
- The final combined build **21:14:00 +0800** (config hash `0x2696ba7a`, code `4d10bf3`) was installed by OTA and boot-confirmed. After the real five-minute idle interval, serial logged **“Idle LED off (zero duty)”** and **“Backlight off; AXP192 LDO2 verified”** together (about 305 s after setup began). Authenticated Web reads with both lights off still reported `Connected (ready)` and unchanged **60.2 V / 5.9 A**. This confirms the idle output commands and continued BLE service, not a measured whole-board power consumption. No physical LED wake observation is claimed for this final build; restoration is covered by the real-wrapper native tests.
- The backlight-only checkpoint OTA installed build **21:04:54 +0800**, config hash **`0x2696ba7a`**, and serial logs confirmed successful boot. Initial read-only Web checks reported `Connected (ready)` and **60.2 V / 5.9 A** readback. No charger setting command was sent by this update.
- During the actual five-minute idle observation, serial reported **“Backlight off; AXP192 LDO2 verified”** about 304 s after setup began (the timer starts after initialization becomes idle). Authenticated Web reads while dark still showed `Connected (ready)` and **60.2 V / 5.9 A**. About 19 s later the real button wake produced **“Backlight on; AXP192 LDO2 verified”**. Subjective LCD appearance remains unconfirmed; short/long/chord wake and consumed gestures are covered by native tests.

---

## Optional board battery voltage and signed current — 2026-09-21

Code commit `f26cfb3` adds the original M5StickC Plus 1.1 AXP192 battery producer and a home footer. Board battery readings use a separate `BOARD_BATTERY` event and timestamp; they neither populate external charger output channels nor depend on BLE connectivity. Positive mA means charging the board battery, negative means discharging. The dedicated power meter is unchanged.

- Native tests compile the real producer, reducer and display against fake I2C. They cover 12/13-bit conversion, reserved-bit masking, signed charge-minus-discharge current, zero, battery absence, ADC enable preserving other bits, no pre-enable sample, wrong chip ID, each read failure, write failure, recovery, invalid typed samples, six-second expiry and clock rollover. The only producer write is ADC register `0x82`; charger control, voltage/current limits and power rails are not changed.
- All **32 event-routing profiles**, existing real-BLE transaction tests, Button/LED tests, telemetry-view tests and 32 Web-entity profiles passed. Fifteen layout renders passed glyph and text-bound checks. The new charging/discharging/unavailable screenshots are synthetic previews, not physical LCD photographs.
- All **32/32 module combinations** passed YAML validation, full ESP-IDF compilation, byte-for-byte custom source checks and compiled-component inclusion checks. [Archived v2.2 matrix](https://github.com/chinawrj/esphome-lxy-charger/blob/v2.2.0/docs/test-matrix.json): `board-battery-20260921T113211Z-e34e444fdb64`; source manifest `e34e444fdb64e8176a5e11e4641f316dd9de57e1a0934935e2c5d21f25c33748` stayed unchanged throughout the run.
- Separate M5StickC Plus, ATOMS3U and generic ESP32 builds passed. Only M5Stick was uploaded; the other two builds remain compile-only.
- Successful M5Stick OTA installed the **19:32:12 +0800** build, configuration hash **`0xbf779a57`**. The first upload was rolled back after opening the USB serial port reset the board before boot confirmation. A second upload with the serial port already open ran the new battery producer. Actual samples read **4.158 V**, **0.0 mA charge**, **0.0–0.5 mA discharge**, giving **0.0 to −0.5 mA net**. A later USB reconnection still produced new-driver readings at **4.154 V**; serial logs then confirmed **“Boot seems successful”**, completing the OTA boot confirmation. This verifies the live reading path, not external-meter calibration or a physical positive-charge test.
- Read-only post-upload Web checks confirmed `Connected (ready)`, **60.2 V / 5.9 A** setpoint readback, voltage range **50.0–93.0 V**, current range **1.0–10.0 A**, both in 0.1 steps. This update sent no charger-setting command. Operator acceptance of the new footer and a physical charge/discharge transition have not yet been reported.
- Independent Linux CI [run 35595178987](https://github.com/chinawrj/esphome-lxy-charger/actions/runs/35595178987) completed successfully: **34/34 jobs passed** for code commit `f26cfb3d6364d316722d8e1d5c58ae3646214be0` (native tests/YAML validation, all 32 module firmware combinations, and ATOMS3U). The v2.2.0 release adds only verification documentation and the matrix report to that tested code.
- External charger live-current decoding remains deferred by the operator; adding the board's own battery current does not change that scope. Register sources and sign convention are linked in the [driver documentation](../components/m5stick_battery/README.md).

---

## Power-focused meter and expanded setpoint ranges — 2026-09-21

Code commit `77fe8c4` includes the power-focused layout (`80e84fc`) and requested setting ranges: **50.0–93.0 V / 1.0–10.0 A**, in 0.1 steps. Voltage follows the operator-reported 50–93 V nameplate. Current follows the operator's explicit 1–10 A request; the reported nameplate says 3–10 A. Acceptance below 3 A has not been physically tested.

- Native real-BLE fake-transport tests passed for all **431 voltage steps and 91 current steps**, including single-send payload bytes, matching echo, independent readback, and rejected nonfinite/off-step/out-of-bounds requests. This is not a claim of testing every V/A pair or electrically operating the charger throughout these ranges.
- Web staging tests cover those same steps without automatic Apply. Real button/controller tests cover both voltage and current endpoints, stopping at bounds, a separate confirmation gesture, unchanged companion setpoints and rejection of invalid baselines. Existing transaction, wake and telemetry regressions passed.
- The meter gives power **76 px** type, with **28 px V/A on one footer row**; longer power strings shrink. All twelve shared-view-model render scenes passed text bounds and glyph checks. Meter previews are synthetic/layout evidence, not physical LCD photographs. The 15-second idle entry and consumed wake gesture are unchanged.
- All **16/16 module combinations** passed YAML validation, full ESP-IDF compilation and exact source-copy/compiled-component checks. [Archived range matrix](https://github.com/chinawrj/esphome-lxy-charger/blob/ef35b26/docs/test-matrix.json): `voltage-current-range-20260921T111823Z-f8dab5dbffd2`. Separate M5StickC Plus, ATOMS3U and generic ESP32 builds passed; ATOMS3U remains compile-only.
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

See the [archived v2.0 matrix](https://github.com/chinawrj/esphome-lxy-charger/blob/d95569441e9371ad8649ee7e20e29b116d0fda17/docs/test-matrix.json) for these historical matrix results. The bit order is LCD, Button, LED, Web; BLE and the event bus are always present. Matrix tests use example credentials and do not upload firmware.

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

# Optional LCD view

`charger_display.h` is the portable, typed view model shared by firmware and
layout previews. It returns positioned labels from the event-bus snapshot and
boot-relative time. The LCD YAML owns AXP192 power, SPI, ST7789V and fonts, and
renders a landscape 240 × 135 page using **ESPHome `mipi_spi`, not LVGL**.
Chinese labels use the bundled ChargerSansSC subset; large numbers use Roboto.

## Three separate kinds of information

The top link label uses `connected` and `connection_enabled`: actual connection,
connection/search enabled, or disconnected/disabled. It does not infer link state
from readiness, a numeric display or the LED's fixed red color.

The large values are measured output only. The smaller “设定” row contains
configuration readback, never a substitute for output. `output_state(now)`
separately distinguishes DISCONNECTED, INITIALIZING, UNSUPPORTED, WAITING, LIVE,
STALE and INVALID. A supported, valid, connected measurement younger than
6000 ms may be shown as LIVE. Other states show `--.-` with an explicit reason,
such as “通信正常，输出数据尚未解码”.

**The current decoder provides provisional voltage only.** Its capability
has `telemetry_channels=1` and `telemetry_inferred=true`. Fresh voltage is shown
in large digits with “电压待核”; current stays `--.-` and “电流暂缓 / A”.
The reason line explains that voltage mapping awaits verification. Unknown,
invalid, disconnected and stale samples remain unavailable. Neither setpoints
nor an unloaded condition generate measurements. OutputState describes data
availability, not charger output enablement.

The board LED is physically red. It is steady for an actual BLE link, blinks
500 ms on/off while connection is enabled but absent, and is off when disabled
and disconnected. It has no transaction/error/charging meaning.

## Local pages and feedback

EDIT, CONFIRM and SUBMITTING display the pair of local draft setpoints. Confirm
also shows the original readback pair and requires a separate long A release to
submit once. REFRESHING retains the output/current-readback page and labels a
read request, rather than showing an old cancelled draft as Apply. CONNECTING
has separate feedback and request correlation. No page action is emitted by the
view model itself.

Local feedback comes from typed `UiNotice`, not string matching or the most
recent background BLE result. It distinguishes APPLIED, REFRESHED, CONNECTED,
CONNECT_FAILED, cancellation, limits, not-ready/busy, timeout and unknown result.
UI_STATE.sampled_at becomes ui_updated_at. On the normal page ordinary notices
are visible for 3 seconds; FAILED, UNKNOWN and CONNECT_FAILED for 6 seconds.
The snapshot keeps the last notice; display expiry does not modify it. Connection,
refresh and busy messages take precedence while active. Background readbacks and
LCD heartbeats do not renew the local feedback timestamp.

`UiHold` explains the action that will occur on release. At 800 ms it can show
“松开 A 进入编辑”, “松开 A 提交一次” or the corresponding review/cancel/help hint.
Crossing the threshold does not send a request. Holds over 5 seconds ask the user
to release and retry, without executing an action. Loss of the editing mode
invalidates the whole held gesture.

View B short refreshes a ready device, connects a disconnected/disabled device
once, or asks the user to wait for an already-enabled connection. View B long
opens HELP, explaining the fixed red LED and controls. Short A/B returns from
Help without also selecting, refreshing or connecting; long presses do nothing.
Help also returns after 30 seconds without input. `UI_CONTROLS` removes control
hints/highlights in a display-only configuration.

## Display capability and validation

Every 250 ms draw callback publishes UI_DISPLAY availability from successful
power initialization and the display component's failure flag. Buttons measure
heartbeat reception and prohibit edits/Apply after 3 seconds without a fresh
available display. Without LCD, buttons may still connect/read, but cannot submit
settings or enter invisible Help. The view references no BLE, Web, Button or LED
objects; all application input is from the bus.

`tests/render_lcd.py` compiles this same model and renders labels with the bundled
fonts to create documentation previews. These are layout previews, not hardware
photos; font rasterization may differ by a pixel. `tests/test_telemetry_view.py`
checks the real reducer and view; `tests/test_local_controls.py` checks hold,
request-correlation and cancellation behavior. Neither replaces a physical panel
and button check.

## Dedicated idle meter

`UI_STATE.ui_mode=METER` selects a four-label page: power in 76 px
Roboto with a W unit, and voltage/current together in a 28 px footer row. Power
strings longer than five characters use 40 px so they fit. No setpoints or operation/status rows are
shown. An amber `V*` retains the inferred-voltage marker. The normal page gives
the full explanation. Power is computed only from finite, declared voltage and
current channels in the same fresh sample. Voltage-only, missing, invalid,
stale and disconnected samples cannot produce a numeric watt reading.

Only Button owns inactivity/wake state; the renderer has no timer side effects
and sends no BLE requests. Dedicated previews cover voltage-only, synthetic
both-channel, and stale data. These are layout renders, not hardware photos.


The optional board-battery event adds a home footer with voltage and signed mA
(`充 +` / `放 -`), replacing the idle button hint while retaining held-action
prompts. Unavailable/stale and absent battery are distinct. The W/V/A meter and
edit/confirm/help pages retain their existing purpose. Without Battery, the
original button hint remains; neither battery data nor BLE updates reset idle.

## Backlight control

The LCD consumes `UI_STATE.ui_backlight_on` from the snapshot and owns the original Plus AXP192 backlight rail. It changes only register `0x12` bit 2 (LDO2), preserving all other rails, and verifies readback before recording success. Failed operations retry on the next render. LCD logic remains powered and the renderer keeps publishing heartbeats while dark. Button absence/unavailability forces the backlight on. The rail assignment follows [M5Stack's original Plus implementation](https://github.com/m5stack/M5StickC-Plus/blob/master/src/AXP192.cpp).

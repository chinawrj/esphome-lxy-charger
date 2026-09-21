# LXY BLE Charger · ESPHome

**English** | [简体中文](README.zh-CN.md)

A BLE charger controller built with **ESPHome 2026.9.0 and ESP-IDF**. BLE and an internal event bus form the required core. LCD, physical buttons, LED, standalone Web and the M5Stick battery monitor are five independent optional modules. The web interface works without a Home Assistant server; a build without Web can still connect to the charger and read its configuration.

The supported protocol uses service `FFF0`, writes to `FFF2`, and notifications from `FFF1`. A matching brand or advertised device name does not establish protocol compatibility. The main LCD view is reserved for **output voltage and current**; charging **setpoints** are shown separately in smaller text. Missing or stale output data is shown as `--.-`, never substituted with setpoints. **Live voltage decoding is provisional**: `84` DATA[3:5], big-endian, divided by 10. Restart captures support this interpretation, but it has not been cross-checked against the original app or a meter. The LCD labels it `电压待核` (voltage mapping awaiting verification). **Live current decoding is deferred**; current remains `--.-` / `NA`, while current setpoint control remains available. Temperature decoding and a charging-output switch are not implemented.

## Screenshots

### LCD layout previews

These previews are generated from the **same C++ view model used by the firmware**. They are not hardware photographs. [The renderer](tests/render_lcd.py) uses Pillow for fonts, so individual pixels can differ from the device.

<p>
  <img src="docs/images/lcd-output-preview.png" width="320" alt="LCD output view with provisional 58.8 V, current unavailable, and separate setpoints">
  <img src="docs/images/lcd-edit-preview.png" width="320" alt="LCD editing preview with a sample 58.3 V and 5.1 A draft">
  <img src="docs/images/lcd-confirm-preview.png" width="320" alt="LCD confirmation preview requiring another A hold before Apply">
  <img src="docs/images/lcd-help-preview.png" width="320" alt="LCD help page explaining the connection-only red LED and button actions">
</p>

The previews show the output view, edit, confirmation, and on-device help. The output preview uses **58.8 V** as a decoder-layout example from a captured byte pattern, with current unavailable; it is not a calibrated measurement or physical LCD photograph. Editing and confirmation use a **58.3 V / 5.1 A sample draft**, not a captured output measurement. The Chinese header explicitly separates BLE connection from output-data availability.

Additional UI test scenes: [unavailable output](docs/images/lcd-unavailable-preview.png), [synthetic live values](docs/images/lcd-live-simulation.png), [synthetic stale data](docs/images/lcd-stale-simulation.png), and [disconnected](docs/images/lcd-disconnected-preview.png). The synthetic 53.8 V / 4.9 A values exercise rendering only; they are not charger measurements.

### Web interface on the device

<img src="docs/images/web-ui.png" width="702" alt="Actual ESPHome web interface with a provisional voltage reading, current unavailable, and separate setpoints">

Captured from a M5StickC Plus running the updated interface firmware after OTA. A read-only local proxy preserved the device's original page and live event data; readings were not simulated or replaced. The image is cropped to omit IP and diagnostic details. `BLE status` reports the link separately. `Output data status` explicitly labels the voltage mapping as inferred and current as unavailable. Readback remains **58.4 V / 5.1 A**. `Live output valid` indicates a fresh, accepted sample for the declared voltage channel; it does **not** certify calibration or make current available.

## Choose a configuration

| Entry file | Hardware | Included modules |
|---|---|---|
| [esp32-minimal.yaml](esp32-minimal.yaml) | Classic ESP32 with BLE, 4 MB flash | BLE + event bus |
| [esp32-headless.yaml](esp32-headless.yaml) | Classic ESP32 with BLE, 4 MB flash | Core + Web |
| [atoms3u.yaml](atoms3u.yaml) | M5Stack ATOMS3U, ESP32-S3, 8 MB flash | Core + Web |
| [m5stickc-plus.yaml](m5stickc-plus.yaml) | Original M5StickC Plus / v1.1, ESP32-PICO-D4 and AXP192 | Core + LCD + Button + LED + Web + Battery |

The M5StickC Plus hardware packages do not support Plus2. ATOMS3U has no LCD package in this project, and the M5StickC pin assignments should not be applied to a generic ESP32 board. ESP32-S2 has no BLE and is not supported. See the official [M5StickC Plus](https://docs.m5stack.com/en/core/m5stickc_plus) and [ATOMS3U](https://docs.m5stack.com/en/core/AtomS3U) hardware documentation.

The M5StickC Plus 1.1 LCD, LED, and serial installation flow also draw on the same author's [m5stickplus1.1 project](https://github.com/chinawrj/m5stickplus1.1), adapted here to independent ESPHome packages.

Choose modules in the entry file's `packages` section. Always keep `common`:

```yaml
packages:
  common: !include packages/common.yaml
  web: !include packages/web.yaml
  lcd: !include packages/m5stickc-plus-display.yaml
  buttons: !include packages/m5stickc-plus-buttons.yaml
  led: !include packages/m5stickc-plus-led.yaml
  battery: !include packages/m5stickc-plus-battery.yaml
```

Remove an optional package line to omit its module and resources. The four M5StickC hardware packages require that board. LCD, Button, LED, Web, and Battery communicate through the event bus; none calls another optional module directly. See the [architecture and event contract](docs/architecture.md) (Chinese).

## Build and install

You need Python, C++ build tools, and an internet connection for the first ESPHome/ESP-IDF dependency download. From the project directory, on macOS or Linux:

```sh
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cp secrets.example.yaml secrets.yaml
```

Edit `secrets.yaml` and set your charger's `charger_mac`. The BLE configuration reads it through `mac_address: !secret charger_mac`. When enabling Web, also fill in the example's Wi-Fi, fallback access point, web login, and OTA credentials. `secrets.yaml` and generated firmware are excluded from version control; compiled firmware can contain those credentials.

Validate, compile, and upload the entry file for your board, for example:

```sh
esphome config esp32-headless.yaml
esphome compile esp32-headless.yaml
esphome upload esp32-headless.yaml --device /dev/cu.YOUR_PORT --upload_speed 115200
esphome logs esp32-headless.yaml --device /dev/cu.YOUR_PORT
```

Replace the YAML filename and serial port as needed. The first upload replaces the board's previous firmware; ESPHome's `upload` command selects the appropriate image and addresses. A core-only build has no Wi-Fi or OTA, so use USB for uploads and logs. See the [ESPHome CLI documentation](https://esphome.io/guides/cli/).

Disconnect the phone app from the charger before connecting the controller. At startup, the controller discovers GATT characteristics, subscribes to notifications, and reads the setpoints. These steps do not send a settings command.

## Use the optional modules

### Web

The controller first tries the Wi-Fi networks configured in `secrets.yaml`. Open its local IP address in a browser. If it cannot connect, join its fallback access point and open `http://192.168.4.1/`, then sign in with your web credentials. The Wi-Fi provisioning page is `/wifi`.

Page assets are stored on the device and do not require an external CDN. Home Assistant's native API is not enabled. Omitting Web also omits this page and its Wi-Fi access point.

| Control or reading | Meaning |
|---|---|
| `BLE status` | Bluetooth connected, connecting, or disconnected; independent of output decoding |
| `Output data status` | Live, not decoded, waiting, invalid, or stale output data |
| `Charger ready` | GATT is ready and this connection has returned its setpoints |
| `Output voltage/current` | Voltage from the provisional status decoder; current is unavailable (not assumed zero) |
| `Live output valid` | Whether a valid output sample is present and less than 6 seconds old |
| `Readback set voltage/current` | Setpoints read from the charger |
| `Target voltage/current` | Local drafts; editing does not send settings over BLE |
| `Apply settings` | Explicitly submit both draft values |
| `Refresh settings` | Read the configured setpoints |
| `BLE connection` | Manage the Bluetooth connection; does not switch charging output |
| `Command status` | Request acceptance, readback verification, rejection, or an uncertain result |

The allowed settings are **50.0–93.0 V and 1.0–10.0 A**, in **0.1** steps. The voltage range follows the operator-reported charger nameplate (50–93 V); the current nameplate is reported as 3–10 A, and the operator explicitly requested software bounds of 1–10 A. Acceptance below 3 A remains unverified on hardware. Encoding, validation and simulated echo/readback are tested across all 431 voltage steps and 91 current steps. Physical setting/readback tests so far cover 58.2–58.4 V only. Changing the allowed range does not apply a new setting.

### LCD

The normal view gives **output voltage and current** the largest type (40 px). Smaller `设定` (setpoint) values are the configured voltage and current. The top-left BLE state remains visible independently of the top-right data state:

| Display text | Meaning |
|---|---|
| `BLE 已连接` | The Bluetooth link is connected; this alone does not establish valid output readings |
| `BLE 连接中` | Searching, connecting, or reconnecting |
| `BLE 已断开` | No connection and connection attempts are disabled |
| `电压待核` | Live voltage from the provisional byte mapping; current is deferred |
| `实时` | A valid sample from a non-provisional producer is less than 6 seconds old |
| `未解码` | The link works, but output decoding is not supported yet |
| `已过期` | A previous valid sample is at least 6 seconds old; large readings are hidden |

A separate line explains initialization, unavailable/invalid data, or the latest local operation. The deployed voltage decoder publishes fresh status readings with **“电压待核”** and **“电压映射待核验，电流暂缓”**. Current remains unavailable. A charger with no battery connected is not sufficient evidence to display a measured **0.0 A**.

The display never substitutes setpoints for output readings. Disconnection invalidates measurements. The voltage mapping is an explicit inference from restart captures; cross-checking its physical accuracy remains outstanding. Layout previews and synthetic telemetry tests establish software behavior, not electrical accuracy. See the revision-specific [verification record](docs/verification.md).

The screen omits IP addresses, branding, and temperature. Its bottom row shows the available button actions. The shared `charger_display` view model supplies the layout for both the device and previews.

### Buttons

Hold for **800 ms to 5 seconds, then release** for a long press. At the threshold, the screen prompts you to release; reaching 800 ms alone does not submit anything. Holds longer than 5 seconds are ignored as stuck input.

| Mode | Button A | Button B |
|---|---|---|
| View | Short: select voltage/current. Long: enter edit mode. | Short: refresh when ready, or request one connection when disconnected and attempts are disabled. Long: open Help. |
| Edit | Short: decrease the selected draft by 0.1. Long: open confirmation. | Short: increase the selected draft by 0.1. Long: cancel. |
| Confirm | Another independent long press submits the paired draft once. | Any normal short/long press cancels. |
| Help | Short: return to the output view without selecting anything. | Short: return without refreshing or connecting. |

While a connection or operation is pending, repeated presses do not submit another request. Help explains the LED and buttons on the device itself; long presses in Help do nothing, and it closes after 30 seconds without input.

Editing and Help require a usable LCD. Without one, the Button package can select a field, connect, and request read-only refreshes, but cannot edit settings or submit Apply. Entering Edit and opening Confirm do not send settings. The confirmation screen shows both draft values and the original readback.

Drafts remain within **50.0–93.0 V / 1.0–10.0 A**, in **0.1** steps. Editing is cancelled after **30 seconds** without input, on disconnection, when another operation makes the charger busy, when the baseline configuration changes, or when the LCD becomes unavailable. Cancelled drafts are not submitted or replayed later.

Local feedback distinguishes **parameters refreshed**, **settings confirmed**, and **cancelled without sending**. A result that cannot be confirmed is labelled **“结果未确认，未重发”** (result unconfirmed; not resent). Ordinary notices appear for 3 seconds; failure, unknown-result, and connection-failure notices remain for 6 seconds. They expire automatically and do not change the LED's meaning. Button operations use events and never call the BLE service directly.

After **5 minutes without button activity**, the Home/Meter backlight and status LED turn off (LED at zero duty, without idle pulses). Press A or B to restore the backlight, resume the BLE LED pattern and return Home; the entire wake gesture is consumed. BLE, Wi-Fi, battery sampling and display heartbeats continue. Editing, Help, held keys and pending operations keep it lit. Builds without Button keep the backlight on.

### M5Stick internal battery

The home footer displays the **board's own battery**: voltage with two decimals,
and signed current in mA. `充 +...mA` means charging; `放 -...mA` means discharging.
A full/idle battery can read `0.0mA` even with USB connected. Read errors and stale
samples show `--`; a missing battery is labelled separately. This is independent
of the external BLE charger and does not enable its deferred current decoder.
Held-button prompts temporarily replace the footer. The W/V/A-only meter is unchanged.

<img src="docs/images/lcd-battery-discharging-preview.png" width="480" alt="Home layout preview with the M5Stick battery voltage and signed discharge current">

This image is a synthetic layout preview, not a hardware measurement.
[Charging preview](docs/images/lcd-battery-charging-preview.png) ·
[Driver, register sources and event behavior](components/m5stick_battery/README.md).

### Idle meter page

With both LCD and Button enabled, **15 seconds without button activity on the home page** opens a dedicated meter: **76 px power as the main reading**, with voltage and current together in a smaller 28 px footer row. Longer power readings shrink to fit. It shows only the three readings and units. Any A/B press returns to the home page; the entire wake gesture is consumed, so it cannot also refresh, connect, edit, or submit. Editing, confirmation, Help, pending requests and held buttons do not switch to the meter. LCD-only builds keep the home page.

Power is **live voltage × live current from the same fresh telemetry sample**, never a product of setpoints. The current voltage-only decoder leaves current and power as `--.-`. Disconnected, invalid or stale readings also become `--.-`. Amber `V*` preserves the provisional-voltage warning; return home for full BLE/data status.

<img src="docs/images/lcd-meter-preview.png" width="480" alt="Idle meter layout preview: large power reading, with voltage and current together below">

[All-channel meter simulation](docs/images/lcd-meter-simulation.png) demonstrates computed power using synthetic values, not device measurements.

### LED

**The LED is physically red. Its colour does not indicate a fault.** It has exactly one purpose: showing the Bluetooth connection state.

The M5StickC Plus indicator uses 1 kHz PWM capped at **10% duty** for a dimmer light. Adjust `max_power` in `packages/m5stickc-plus-led.yaml` if needed; this does not change the connection patterns below.

| Indication | Meaning |
|---|---|
| Steady on | Bluetooth connected, including while initialization or an operation is in progress |
| Slow flash: 500 ms on / 500 ms off | Searching, connecting, or reconnecting |
| Off | No link and connection attempts are disabled |

While awake, **steady = connected, flashing = connecting, off = disconnected**. During five-minute display idle, the LED is intentionally off even if BLE remains connected. Missing output decoding, stale readings, busy operations, and command results do not replace this pattern. Read the screen or Web status for those details. Long-press B in the normal view to see this explanation on the device.

## How settings are confirmed

Each Apply sends one `03` settings frame, waits for an `83` echo with the requested voltage and current, then sends a new `02/01` query. The returned `82` setpoints must match before the result becomes `VERIFIED`. `ACCEPTED` only means processing has begun. The `83` echo has no independent success code.

Background status polling uses the same serialized request channel: while `04` awaits `84`, one read or Apply request can wait with its own ID and frozen values. It is sent only after the status response. Background polling alone does not prevent local draft edits.

A timeout or disconnection clears the transaction and reports failure or an unknown outcome. Settings are never replayed automatically. Startup, reconnection, reads, and opening the web page do not Apply settings. Web drafts survive a BLE reconnect; after a controller reboot, the first valid readback initializes them.

Readback confirmation does not establish power-cycle persistence or actual electrical output. The output-enable/disable command has not been implemented. See the [captured protocol and its limits](docs/protocol.md) (Chinese).

## Tests and verification

| Check | Coverage | Limits |
|---|---|---|
| Native C++ tests | Captured frame decoding; event queues, IDs and invalidation; simulated event routing for all 32 module combinations; the real BLE transaction implementation with a fake clock and GATT transport | Does not validate radio behavior, GPIO wiring, or physical display appearance |
| ESPHome matrix | Required BLE + bus with all `2^5 = 32` LCD/Button/LED/Web/Battery combinations on M5StickC; a separate ATOMS3U build | Does not mean every combination was flashed to hardware |
| Recorded hardware checks | Current M5StickC Plus interface: the operator confirmed the Chinese LCD, button responses, steady red LED, and a 58.9 V decoded voltage display after charger restart. Historical firmware: headless + Web and full-profile BLE reads, explicit Apply with readback, and reconnect behavior passed | ATOMS3U hardware and a new-interface physical Apply test have not been verified; button-response confirmation does not establish a settings write |

The local-controls test compiles the real button and LED adapters and checks confirmation, cancellation, input loss, and connection-only indication timing, hold/release prompts, and Help behavior. The telemetry view and Web tests use synthetic typed events to check freshness, invalidation, and presentation; they do not establish the charger's output byte mapping. The Web test compiles the real adapter for all **32 combinations of five optional telemetry/status entities**. This is separate from the **32 combinations of the five optional hardware/Web modules**.

The [verification record](docs/verification.md) identifies the tested revision and physical checks. The [32-combination report](docs/test-matrix.json) records build results. Check those records for the revision you are using; prior acceptance does not automatically verify a changed interface.

Run native tests:

```sh
python components/lxy_charger/test_protocol.py
python tests/test_event_core.py
python tests/test_ble_serialization.py
python tests/test_local_controls.py
python tests/test_battery.py
python tests/test_telemetry_view.py
python tests/test_web_telemetry.py
```

Run the module matrix:

```sh
python tests/matrix.py --mode validate --cases all
python tests/matrix.py --mode generate --cases all
python tests/matrix.py --mode compile --cases all --jobs 2
```

Mask bits 0/1/2/3/4 select LCD/Button/LED/Web/Battery. For example, `--cases 0,31` selects the core-only and full builds. Results and logs default to `.esphome/matrix/`; change the location with `--work-dir`. The matrix uses example credentials and compiles without uploading to a device.

Configuration validation, code generation, and firmware compilation are separate checks. Native module substitutes verify event boundaries; they do not replace compilation of the real ESPHome adapters or hardware checks.

## Project layout

- `components/charger_event_bus/`: portable event queue and snapshot, plus the ESPHome wrapper.
- `components/lxy_charger/`: BLE service, GATT lifecycle, protocol decoder, and request confirmation.
- `components/charger_web/`: optional web entities, local drafts, and user requests.
- `components/charger_display/`: shared LCD layout and view model.
- `components/charger_buttons/`: local button editing, explicit confirmation, and cancellation.
- `components/charger_indicator/`: independent, nonblocking LED patterns.
- `components/m5stick_battery/`: optional AXP192 board battery sampling and events.
- `packages/common.yaml`: required core.
- `packages/web.yaml`: optional web interface, Wi-Fi, and network events.
- `packages/m5stickc-plus-*.yaml`: independent LCD, button, LED and board battery hardware packages.
- `assets/`: display font and its [OFL license information](assets/README.md).
- `tests/`: native checks and the configuration/build matrix.

Original captures, device backups, private credentials, and firmware containing credentials are excluded from the public source distribution.
